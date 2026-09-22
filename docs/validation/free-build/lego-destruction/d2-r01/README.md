# D2 source-preserving wall release — integration candidate

Date: 2026-09-18. **D2 gate remains open. Cannon impacts do not break the house.**

## Implemented

The actual Blacksmith model is split into its fixed remainder and 39 original
source parts sharing 11 meshes. All 2,140 source instance identities and source
transforms remain in `assembly.json`. No substitute box debris is rendered.
The selected graph has 46 validated stud bonds and four explicit boundary
anchors. Release affects 29 parts; ten whose external connectors are unsupported
stay fixed. This is an upper facade test, not a complete house support graph.

The runtime loads the pinned `wall.json` and mesh, publishes intact collision,
and owns the wall separately from ordinary scenery. At the cannon, Release wall
cuts the selected graph. **Remove support brick** explicitly removes one named
3005 and releases the remaining eligible pieces; its upper neighbour loses
physical bearing. The existing GPU world owns falling and contact response.
The player stays safely at the cannon while the pieces move. Save/load and edits
that would suppress the damaged house are refused. Rebuild restores source poses.
These changes are session-only.

Rendering selects accepted or pending body handles by the encoded replacement
tick, avoiding a missing-parts frame while CPU completion arrives. Once every
released body is confirmed asleep, an owned zero-tick capture verifies its pose at
the fully joined frontier. Its submission must complete before acceptance.
The runtime then installs static rubble and matching conservative walking,
picking and camera boxes. Moving rubble interaction and waking settled rubble
are intentionally unfinished; they are required before unrestricted destruction.

## Independent review and corrections

[Final support-removal review](manual-support-review.md) accepts the bounded
manual behaviour and records the remaining limits.

- A failed partial GPU admission could trap the player. Explicit Rebuild now
  drains the failed and accepted resources before reinstalling the source.
- A scenery edit could erase changed parts. Commit refuses suppression of a
  released house, and collision publication refuses omission of the house.
- A failed CPU collision publication after reset could unlock stale geometry.
  Query failure now independently holds the safety lock.
- Old body handles could disappear on the replacement frame. Encoded-tick
  render membership now switches with that GPU mutation.
- Public action dispatch originally ignored new actions 35/36 (and later 37); the actual
  runtime integration test exposed and fixed this.
- Session changes mark the browser dirty. Empty body bindings explicitly clear
  the renderer's borrowed physics view. Initial admission failure exposes Rebuild.

## Evidence and remaining acceptance

[Complete affected physics suites](affected-physics-suites.md): 110 cases pass
across contact geometry, solver, islands, topology, stacks and wall ownership.

- Exporter: 13 Python checks pass; Blender comparison reports zero vertex error
  for selected source geometry and validates all source transforms.
- Owner protocol: seven protocol checks plus the manual support-removal case
  pass. Zero-tick certification preserves a borrowed accumulator clock.
  See [owner contract and test log](wall-owner.md).
- Actual house: both supported-release and explicit-support-removal cases pass.
  [Native evidence](manual-support-world.xml) records 52 and 72 ticks respectively.
  The removal case vacates five old centres in the surviving upper brick, not
  just cells belonging to the deliberately removed brick. Both rebuild correctly.
  Source `415964676630277730` is removed, leaving 38 bindings and 28 dynamic
  eligible parts; the upper `409735568841264200` drops under gravity.
  This is a small upper-facade test, not a player-sized opening or cannon damage.
- UI: eight checks pass, including Release → busy controls → Rebuild dispatch.
- Six stale salvage fixture expectations are repaired; all 33 focused fixture
  tests pass. See [gate repair](gate-repair.md). The full hook has not been rerun.
- The actual full-world runtime test passes release dispatch, stationary player,
  blocked fire/exit, save/load refusal, dirty state and paused physics.
- Historical failure before the contact fixes: 28/29 dynamic bodies stayed awake after
  1,200 ticks, with some acquiring extreme velocities. Investigation found
  substantial overlap between selected parts and the coarse character-oriented
  remainder bake, including morphological filling. This is a collision defect,
  not evidence for raising sleep thresholds or forcibly freezing moving pieces.
- The remainder bake now refines only the selected wall region at .2-stud
  resolution, without morphological filling. The installed 5,921-box candidate
  has no selected-shell/stud overlap, including after GPU quantization. Geometry
  outside that region retains the same occupied volume. See
  [source membership](remainder-source-membership.json) and
  [current clearance audit](stud-clearance-remainder-audit.json).
- Stud contact strips now fit inside the actual .3-stud source radius. This
  corrects a proxy protrusion without changing any rendered source geometry.
- The intermediate geometry-corrected run stopped launching pieces away, but
  still failed sleep certification after 1,200 ticks. Explicit rebuilding, restored
  original collision, saving, leaving the cannon and resumed building pass.
  See `runtime-before-contact-fix.xml` and `runtime-before-contact-fix.log`.
- An isolated three-source-brick stack reproduced that jitter without the house,
  allowing the contact solver to be repaired independently of the bake.
  Contact-cache anchor matching and bounded inter-patch candidate coverage now
  pass direct regressions. Three real catalog stacks (3005/3004/3023) settle in
  34 ticks, including a house-origin/yaw variant. The actual facade still has
  physical jitter: a bounded pose trace confirms movement, not merely stale
  velocity. Eight normal patches, cache repair and sixteen substeps subsequently
  resolved this bounded section; current successful native results are above.
- Fixed scenery no longer couples otherwise independent dynamic sleep islands.
  Both compact and general paths retain static collision records. Seven direct
  island checks pass; see [static island correction](static-islands.md).
- Scenery capacity accounting now counts actual colliders instead of admission
  envelopes, and temporary capacity refusal no longer permanently suppresses
  trees. Full-world visual acceptance remains pending.

Browser physical acceptance, a walk-through opening, the normal repository hook
and gate commit remain outstanding. No deployment or commit is claimed here.


## Complete scene resource budget

Restoring nearby scenery exposed an old 8,000-cell admission ceiling. The
complete installed scene now admits 8,694 cells, 43,191 exterior faces, 17,317
BVH nodes and 3,067,344 payload bytes, with 71 bodies and all 160 nearby props.
A scoped cannon/section pool reserves 32,768 cells, 196,608 faces, 65,536 nodes,
24 MiB CPU payload and the existing 32 MiB GPU budget. One complete scenery
revision is capped at 12,000 cells / 72,000 faces / 24,000 nodes / 8 MiB, leaving
room for both scenery revisions plus wall revisions. Engine defaults are
unchanged; body and shape-slot admission still refuses incomplete transitions.

## Compound-contact follow-up

The source audit and compiled CPU regression cover all 741 distinct selected
part pairs. They find no initial penetration above .002 stud; maximum numerical
intersection is below .000002 in source and distant/yaw-π frames. Bonds are cut
before compiling the regression so a union cannot hide an overlap inside a root.
See `selected-proxy-overlap-audit.json` and
`ImportedAssembly.ActualSelectedPartCellsDoNotInitiallyInterpenetrateThroughBodyFrames`.

The synthetic 39-part wall still fails after 1,200 ticks. Controlled shader A/B
runs show the slop correction changes collapse timing; the highest velocities
are downward motion above the floor, not evidence of explosive energy. Neither
variant passes settlement. Logs: `synthetic-wall-ab-a-slop-only.log` and
`synthetic-wall-ab-b-normal-only.log`.

A bounded eight-patch narrow phase is under validation. It gives each independent
contact normal its own complete manifold/friction state and matches old patches
one-to-one. Free Build uses an 8,192-pair/history budget and 4,096 active contacts;
eight raw history slots per pair consume 48 MiB across both 384-byte history
buffers. Ordinary scenes retain their separately configured budgets.

Overflow latches a GPU fault. The solver stops contact response and pose/velocity
integration; mandatory completion proof rejects the batch even with optional
telemetry off. This is fail-stop, not rollback: forces/CCD earlier in that tick
may already have run. No failed tick can certify settled geometry. The runtime
holds its safety lock and asks for a world reload; rebuilding cannot revive a
failed GPU world. Focused and complete-world checks are still pending.

The four-slot catalog/wall run failed closed on contact capacity (3004 at tick
3, 3023 at tick 8, the full wall at tick 3); it did not reach settlement. The
3005 identity and distant variants passed. Eight slots are now being validated;
see `multipatch-four-slot-stack.log/.xml` for the diagnosed insufficient budget.

The complete 10-test solver target and mandatory owned-proof overflow regression
pass (`multipatch-solver.*`, `contact-overflow-proof.*`). This includes configured
contact slop, all three solver paths refusing motion on a latched fault, and
late constraints in 156/516-record repeated-pair chains. Independent review
caught the obsolete twice-body-count dependency bound; bounded dependency levels
now cover at most 512 records and larger batches use the rank-ordered fallback.

## Convergence and borrowed-clock certification

With the same geometry, gravity, friction, damping and sleep thresholds, the
mixed synthetic wall failed at four substeps (28 awake) and eight (14 awake),
but certified sleep at sixteen (718 ticks before the certification update).
The actual house at sixteen also put every released body to sleep. That run
then exposed a separate owner bug: `scheduleFixedTicks(1)` cannot take over an
accumulator-driven world. The owner now requests a completed, incarnation-matched
snapshot at the already joined tick, without scheduling a tick or changing the
host clock. An accumulator-mode regression is being validated.

Free Build now explicitly uses sixteen solver substeps; ordinary engine defaults
are unchanged. GPU stage measurements and full-world/browser acceptance remain
separate gates. No claim of frame-rate performance is derived from test-process
elapsed time, which includes startup and blocking readbacks.
