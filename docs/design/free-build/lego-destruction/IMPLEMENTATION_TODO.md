# LEGO cannon and structural destruction

Status (2026-09-18): D1 aiming/static-target firing and D2 source-aware release,
settlement and rebuilding are implemented as candidates. D3 now routes confirmed
GPU cannon hits to a bounded ground-wall release with an explicitly approximate
impulse transfer. The actual-world test now proves a local brick knock-out: the
struck piece moves 2.64 studs, settles, and rebuild restores collision. The coupled impact design,
walk-through opening, browser acceptance and all D1–D3 gates remain open.
This is a self-contained track for Free Build, not a return to the cancelled
adventure game. Read root `AGENTS.md`, `README.md` and this file before starting.

## What the owner wants

Fire an authentic LEGO cannon at the imported Medieval Blacksmith house. Real
LEGO pieces should break free, tumble, collide and settle. A damaged support
should allow the roof above it to collapse. Use the project's existing GPU
physics engine. Preserve the warm world, current minifigure, free building,
walking, motorbike, saves and minimal UI. Use actual LDraw parts, not generic
cube debris or an explosion animation standing in for physics.

Keep progress messages short: the owner is dyslexic. Each visual check must
answer a specific changed-behaviour question. Request an independent agent
review at each meaningful milestone; do not repeatedly capture unchanged views.
Mark only verified work done. Commit successful gates with the normal hook;
never bypass failing checks or include unrelated dirty work.

## Decisions and desired behaviour

- The existing WebGPU `PhysicsWorld` remains the only rigid-body authority.
  CPU gameplay owns part identities, connections, damage and accepted topology.
  GPU owns motion, contact response, gravity, friction, bounce and sleeping.
  CPU pre-authorizes possible topology changes; GPU may select only those
  prepared changes at the impact boundary, with certified results mirrored back.
- Use compound rigid sections for connected parts, with an explicit connection
  graph. Release smaller authentic clusters/pieces as connections fail. This
  preserves the shape and weight of a LEGO assembly without making every intact
  brick a separate solver body from startup.
- A glancing shot should knock loose a few pieces. A stronger shot should make
  a local hole. Support loss should produce a readable, delayed collapse. Pieces
  remain recognizable; the entire house must not explode on every hit.
- Cannonballs follow physical trajectories. Recoil and impacts have short toy
  sound/visual feedback. Effects cannot fabricate hits or hide bad contact.
- Prototype on a small authentic wall first, then integrate the entire house.
  This is an implementation order, not permission to replace the requested house.
- First prototype may explicitly be session-only. Durable destruction state is
  required before its release gate. Never silently reset damage when editing a
  player-built structure.

## Starting assets and integration

House source: `data/adventure/ldraw-blacksmith-r01/`, community LDraw
reconstruction of LEGO set 21325. Its source MPD and editable
`blacksmith-assembly.blend` retain individual parts. The original static
material-batched asset (851,880 triangles, 44 materials, one mesh/node,
81,984,851 bytes) is historical input, not the current source-aware runtime mesh.
The house plot is `(1208, -1032)` in X/Z, rotated 180 degrees. Respect existing
save-conflict suppression, source attribution and manifest.

Active runtime house package: `data/adventure/ldraw-blacksmith-ground-r01/`.
All 2,140 source identities are preserved. Twenty original ground-wall pieces
use ten shared part meshes plus the fixed remainder mesh (11 nodes total).
Eighteen eligible pieces form one connected component; two unknown-support
plates stay fixed. There are 30 internal and 44 boundary stud bonds, with 6,409
fixed-remainder collision boxes. The render mesh is 19,606,015 bytes, SHA-256
`870af9c0eac96fe04795e5b5b1848c3de8265fde57dfe879020786c7c78aa817`.
Only the fixed remainder render geometry is reduced; the movable parts and
collision source remain at their original detail.
The generated header is `src/game/adventure/ldraw_blacksmith_ground_remainder.hpp`.
The older 39-part upper-wall package and its D2 evidence remain historical
fixtures; do not overwrite them or mistake their counts for the active selection.

Cannon: `data/adventure/ldraw-cannon-r01/`, actual LDraw `2527c01.dat` assembly:
`2527.dat` base and `518.dat` barrel. Red base, source dark-grey barrel, original
15-degree elevation; no wheels invented. Source is community CAD, not a
LEGO-company asset. The project-authored wrapper is not a complete set
reconstruction. "Non-Shooting" is the real part variant's name; simulated firing
must be implemented separately. Source package has 27 dependencies, seven
authors, original CC BY 4.0 notices and a byte/dependency validator.

All models use one world unit per stud (20 LDU), canonical Y-up. Derive and
validate the muzzle origin, barrel axis and pivot from source transforms. Do
not assume model front conventions are identical to the house. The runtime cannon now has separate base/barrel meshes with identity nodes
in shared grounded coordinates. `articulation.json` pins the original hinge
and muzzle frames. Keep elevation in [0.02, 0.34] radians: raising this real
barrel farther makes its breech intersect the ground. Preserve the source.

## Reuse map

Paths below are relative to the repository root.

| Concern | Existing implementation |
| --- | --- |
| Fixed GPU simulation, body commands and events | `src/physics/physics_world.hpp`, `src/physics/gpu/gpu_physics_backend.cpp` |
| Sphere projectiles | `BodySpawnDesc`, velocity, material and `bullet` fields; `spawnBody` |
| Static/dynamic compound shapes | `src/geometry/box_union.hpp`, `src/physics/authored_shape.hpp`, `authored_shape_resources.hpp`, `authored_body_frame.hpp`; `spawnAuthoredBody` |
| Exterior compound contact features | `shaders/physics_narrow_phase.wgsl`, `PackedShapeFace::source` and face ordinal |
| Impact evidence | `PhysicsEventType::ContactHit` in `src/physics/physics_types.hpp`; certified `pollEvents` batches |
| Atomic physical replacement and safe lifetimes | `prepareMutationBatch`, `commitPrepared`, `PhysicsMutationJoin`, submission tickets, generations, shape retirement |
| Graph splits and fragment motion | `src/game/construction/assembly_fracture.hpp/.cpp`, `src/game/expedition/cove_rigid_roots.hpp/.cpp`; Cove integration in `src/app/application.cpp` |
| GPU-resident render transforms | `src/render/mesh_path.hpp`: `MeshDrawInstance::physicsBody`, `setAuthoredBodyView`; `shaders/mesh_path.wgsl` |
| Current building/player queries | `src/game/adventure/adventure_runtime.cpp`, `spatial_queries.hpp`, `ldraw_blacksmith_geometry.hpp` |
| Existing query sweep mathematics | `shaders/physics_queries.wgsl`, `physics_authored_queries.wgsl` |

## Gaps that must not be skipped

1. **Projectile CCD now handles terrain and fixed authored bodies.**
   `shaders/physics_ccd.wgsl` sweeps sphere projectiles against authored exterior
   faces using a compact static-target list, preserves velocity for contact
   response and integrates only the remaining tick after impact. One earliest
   impact is supported per tick. Moving-target and sequential-impact CCD remain
   open. CPU asynchronous queries cannot retroactively correct a shot.
   Include relative target motion before claiming reliability against falling
   sections. Reuse exterior surface filtering so internal proxy faces do not
   become false impact surfaces.
2. **Only a bounded house section has source-aware breakable topology.**
   `CannonPhysicsScene` admits the full fixed remainder and nearby world geometry;
   `ImportedWallPhysics` owns the selected source roots. The current remainder
   has 6,409 baked boxes. Its IDs are not original LDraw part IDs. Impact mapping
   resolves accepted authored faces through the selected root's source labels.
   Preserve this distinction when extending selection to the rest of the house.
3. **LDraw placement does not supply validated connections.** Build a connector
   catalog for studs, anti-studs, hinges and clips actually used. Infer candidates
   from transforms, then validate overlap, orientation and connection kind.
   Explicitly identify grounded foundations. Proximity alone must not glue the
   horse, cart, furniture or ornaments to the walls.
4. **Existing joints are not LEGO weld joints.** Distance attachments model
   tension-only ropes/winches; there is no fixed six-degree-of-freedom joint API.
   Existing welded parts are compiled into rigid roots. Use graph/compound
   splitting for this track. A new general breakable joint solver is a separate
   project and is not a hidden prerequisite.
5. **Existing build topology cannot fit this imported set directly.** It has
   limits of 256 parts, 1,024 connections and 64 roots (Cove uses 32), and its
   grid transform admits 24 cube rotations. The house has over 2,000 parts with
   arbitrary angled pieces. Introduce a bounded imported-assembly representation
   and adapters; never truncate parts or quantize away the original roof angles.
6. **A visible hole must be physically open in every system.** Publish surviving
   render parts, GPU roots and CPU static walking/camera/picking geometry in one
   accepted revision. Static CPU boxes must not represent moving debris. Define
   a GPU query/completed-observation bridge for player and bike interaction.
   Initially, climbing moving rubble may be unsupported, but walking through it
   or retaining invisible walls is not acceptable. Promote settled rubble to
   walkable support only after certified sleep and revision-safe publication.
   Wake/re-impact must invalidate that support at the same accepted boundary;
   handle support loss for a standing player instead of leaving a stale floor.
7. **Damage saves and explicit world reuse still need implementation.** Free
   Build menus now pause physics; projectiles expire by encoded ticks. Reset
   retries refused destruction commands. Application destroys the runtime then
   immediately shuts down its owner world, with no intervening simulation. A
   future transition that reuses that world must explicitly drain authored roots
   before releasing the runtime. Shots and aim are session-only.

## Impact transfer: avoid creating energy twice

A static house has zero inverse mass/inertia and no post-impact velocity.
`AssemblyFracturePlan::inheritMotion` correctly copies parent translation and
rotation (`v_child = v_parent + omega cross offset`); this alone cannot launch
pieces from a static wall. If the ball already bounced from the static parent,
adding the full reported impact impulse again to new fragments duplicates the
response and can manufacture energy.

The target design is one coupled GPU response using the released section's
actual mass, centre of mass, inertia and pre-impact relative velocity. Stage a
bounded hierarchy of possible child shapes and reserve resources before the
impact boundary. Detect time of impact, choose the permitted connection break,
replace/suppress the parent contact, then solve against the released mass once.
WGSL cannot allocate CPU topology during contact solving: CPU pre-authorizes
child partitions, thresholds and resources; GPU selects among those candidates
at time of impact and emits an accepted break record. CPU mirrors the certified
result instead of making a late second decision. Implement a staged activation
protocol with tick/generation ownership, including the CPU query visibility
boundary, and no partial state. Without a ready candidate or reserved capacity,
resolve an ordinary intact-body contact; do not wait mid-solve or half-release.
An asynchronous ContactHit → CPU split after the old response is not this design.

Use certified ContactHit evidence for diagnostics, subsequent damage and effects:
tick, both body generations, source features, local anchors, pre-solve closing
speed and normal impulse. Reject stale, duplicate, overflowed or incomplete
evidence. Solver iteration count or persistent resting contact must not repeatedly
damage the same bond. Multi-contact events require explicit pair/feature/tick
deduplication and damage aggregation.

First prove a manual graph cut followed by GPU gravity collapse, independent of
impact transfer. A loose brick stack can also prove the existing solver transfers
projectile momentum, but it does not prove stud adhesion. If a temporary stylized
release is implemented instead of coupled release, label it approximate and cap
added fragment kinetic energy against an explicit available-energy/dissipation
budget. Never claim exact conservation or apply the full old impulse twice.

Use a coherent gameplay unit/mass convention. The geometry is enlarged to one
unit per stud; blindly applying physical ABS density as if one unit were one
metre produces nonsense masses. Record chosen gravity, projectile mass, piece
mass rules, inertia scaling, friction, restitution and bond thresholds together.

## Current D3 candidate and handoff

This is an interim implementation, not the coupled design above. A certified hit
selects the struck original source and validated connected neighbours within
2.5 studs in the current accepted pose (distance to oriented catalog envelopes,
not centres). At most eight pieces are directly selected, with at most 32 total
after support propagation. Unknown support stays anchored. The old projectile
retires atomically with replacement-root admission; it cannot remain in flight
while its full impulse is applied again.

The transferred energy is at most `min(36, .2 * initialProjectileEnergy,
.2 * measuredClosingKineticEnergy)` game joules. Total linear impulse is at most
20% of measured normal impulse; COM speed is at most 12 studs/s and angular
speed 10 rad/s. Only the struck root receives the direct impulse and torque,
using its packed mass/inertia and actual contact lever. Other released pieces
start at rest and receive forces through GPU contacts/gravity. A common scale
caps combined translation and rotation for that struck root. No exact conservation claim is made for this delayed, dissipative
approximation. Subsequent motion and sleep are GPU simulated, with certified
static query publication after settlement and current-pose reuse on re-impact.

The event pipeline now reads dense solved active manifolds. The old sparse raw
history scan could lose hits at later patch slots. Preserve the regression and
ordered batch checks: incarnation, tick continuity/completion, overflow, body
generation and accepted geometry revision. Do not interpret raw manifold source
ordinals as LDraw IDs.

User entry: **Visit cannon** checks a supported clear landing beside world
`(1240, .185, -1027)` and enters the normal cannon control. Heading is `-pi/2`,
initial relative yaw `-.0360332748563`, elevation `.0726767584712`, target source
`93361846531299384`. **A/D**, **W/S**, **Space / Fire**, **C / Leave cannon** are
the controls. One outstanding shot is permitted despite eight storage slots.
Leave, walking and bike use cannot bypass shot/settlement locks. Rebuild restores
original source collision and saveability; damage remains session-only. Manual
release/support-removal controls are test-only, not player-facing Fire substitutes.

Current full-house regression passes: one real shot releases eight pieces; the
struck source moves 2.640758 studs, using 18.889066 game joules, and settles after
136 ticks. The unchanged >0.5-stud source-pivot assertion rejects proxy deletion
alone. ReleasedShell omits selected stud strips but preserves every shell, source
transform and original full-proxy mass/inertia; rebuilding restores Detailed.
The previous broadcast impulse/torque jammed neighbors together; localized
contact transfer fixes it with less energy. Retained corner hardware still
means a route into the room is not proved even with selected parts absent.
Read `docs/validation/free-build/lego-destruction/d3-r01/README.md`, run its actual
world case after physical changes, then verify ordinary browser controls and get
independent visual critique. Update evidence only with observed results.

## Budgets to measure, not promises

| Resource | Current constraint / implication |
| --- | --- |
| Free Build bodies | `free_build.cfg` now reserves 256 GPU bodies for old/new scene roots and shots |
| Engine defaults | 16,384 bodies, 65,536 pairs, 16,384 contacts, 65,536 manifolds; application overrides may differ; inspect actual selected config |
| Engine default authored resources | 256 live plus retiring shape slots; 16,384 cells, 98,304 faces, 32,768 BVH nodes; 16 MiB CPU payload and 32 MiB GPU budget |
| Free Build contact profile | 8 normal patches per pair; at most 8,192 pairs/history groups and 4,096 active contacts; 16 solver substeps. Two expanded histories consume 48 MiB. Default scenes remain one-patch/four-substep unless explicitly configured. |
| Cannon/section scene profile | 32,768 cells, 196,608 faces, 65,536 BVH nodes; 24 MiB CPU payload, 32 MiB GPU; complete scenery revision capped at 12,000 cells / 72,000 faces / 24,000 nodes / 8 MiB. Old + replacement revisions coexist with separately owned wall roots. Existing body/slot capacity checks still refuse an overfull transition atomically. |
| One BoxUnion default | 2,048 input boxes, 4,096 cells, 24,576 faces, 8,388,608 clipping tests; the 4,054-box house bake cannot fit directly |
| CPU static queries | 8,192 solids shared by house/scenery/building; 16-stud buckets |
| MeshPath | 8,192 instance/submesh records and draw ceiling; multiple materials expand records before batching; shadows add work |
| Browser heap | CMake path uses fixed 768 MiB; root Bazel BUILD still uses 512 MB; both disable growth. Validate and reconcile the selected shipping path; house decode/upload is already substantial |

Proposed starting ceiling for the full-house experiment: eight live cannonballs,
32–64 active section roots and 128–256 loose-brick bodies. These are design
targets, not validated performance or configured values. Count terrain/scenery,
old and replacement roots, pending/retiring shapes and shots in flight. Fail
admission safely; do not silently drop collision, parts or contact events.
Reuse per-type mesh/shape data and sleep settled debris. Any debris merging must
preserve part inventory, visible pose, collision and saved state. Do not introduce
per-frame CPU readback of every rigid-body transform to render it.

## Ordered implementation checklist

### D0 — authentic asset and design

- [x] Investigate existing GPU physics, building import and collision integration.
- [x] Package actual LDraw cannon sources with full dependency/attribution closure.
- [x] Import editable assembly and export runtime GLB/VMESH at one unit per stud.
- [x] Validate source closure and cook the unchanged 2,368-triangle cannon.
- [x] Independent asset and architecture critique incorporated; record evidence
  in `docs/validation/free-build/lego-destruction/README.md`.
- [ ] Pass normal repository checks and commit this gate without unrelated files.
  Latest complete run: 2,513 passed, 16 skipped, 6 existing salvage fixture
  accounting/draw-count failures. Their expectations were repaired on 2026-09-18
  and all 33 focused fixture tests pass. A fresh complete gate is still required.

### D1 — cannon and reliable physical projectile

- [x] Add cannon as an admitted Free Build world object near the house, respecting
  existing construction and collision. Include asset packaging and credits.
- [x] Export barrel/base separately with a validated muzzle and hinge frame.
  Add bounded yaw/elevation, explicit enter/leave interaction and a small aim/fire
  hint. Preserve brick placement and camera controls outside cannon use.
- [x] Bind the existing GPU world to Free Build; spawn a capped sphere projectile
  with physical velocity, gravity, material, lifetime and safe muzzle clearance.
  The base is anchored; barrel recoil is cosmetic, not a dynamic recoil body.
  Shots: radius .44 stud, mass 2 game units, 48 studs/s, restitution .18, friction
  .55, rolling resistance .04, one-second reload, 2.5-second lifetime, eight slots
  with one outstanding shot permitted in the current D3 interaction.
  The 140-stud collision region contains the maximum 120-stud horizontal travel.
  Existing world gravity applies; this is not a real-world artillery model.
- [ ] Prove a shot transfers momentum to a small dynamic authentic brick stack.
  Keep this distinct from the later connected-wall test.
- [x] Add static authored-body CCD: thin wall, oblique/grazing shot, opening
  miss, distant-sector precision, later-wall contact in the same compound, slow
  spheres and zero speculative margin. Preserve incoming velocity for the solver.
- [ ] Extend CCD to moving targets and multiple sequential impacts within a tick;
  validate repeated shots at the dynamic authentic brick stack.
- [x] Verify complete collision admission, capacity refusal/rollback, pause,
  reload/lifetime and safe shot retirement through GPU component tests.
- [x] Verify actual runtime C/aim/Space/cooldown/pause/leave and resumed brick
  placement on the installed terrain with certified GPU submissions.
- [ ] Finish browser firing/pause/reload/leave acceptance and inspect projectile
  shadow/render pose and no self-hit in ordinary play.
- [ ] Gate: one reproducible native and browser interaction, focused tests,
  independent review, normal hook and commit. Do not claim house destruction yet.

### D2 — authentic part topology and gravity collapse

- [x] Export stable source-instance IDs, part number, colour, local transform,
  mesh reference and connector/proxy metadata. Share repeated geometry. Preserve
  arbitrary source rotations, assembly hierarchy, source hashes and attribution.
- [x] Define a versioned imported-assembly schema independent of restrictive
  player-build grid limits, with explicit capacities and validation failures.
- [x] Select a bounded original wall region; create validated stud connections
  and explicit boundary anchors. The original 39-part upper-facade fixture remains;
  active runtime now selects 20 ground-wall pieces, with 18 eligible for release.
  Record unsupported part types instead of guessing attachment strength.
- [x] Compile compound proxy roots with accurate exterior faces, source labels,
  masses/COM/inertia. Wait for resource readiness before physical admission.
- [x] Implement a manual test cut, connected components and loss-of-support
  propagation for this bounded section. A separate explicit support-removal
  action deletes one source brick from render and collision; its surviving upper
  brick falls under GPU gravity. Cutting bonds alone may leave bearing pieces standing.
- [ ] Atomically replace render membership, physics and walking/picking/camera
  geometry; test safe refusal/rollback and no duplicate/missing source parts.
- [x] Correct silent repeated cannon refusals and add close inspection framing.
  The old walk-near feedback is recorded in
  `docs/validation/free-build/lego-destruction/d2-r01/cannon-feedback.md`; current
  distant entry is explicitly labelled Visit cannon and checks safe travel.
- [ ] Gate: real parts fall and settle; the exposed opening can be walked through;
  native/browser checks, independent review, normal hook and commit.

Historical D2 upper-wall implementation/evidence (not the current ground package):
`data/adventure/ldraw-blacksmith-parts-r01/`
preserves 2,140 source instances and exposes 39 exact CAD parts using 11 shared
meshes. The bounded graph has 46 validated stud bonds, four boundary anchors,
29 eligible pieces and ten pieces with unsupported external connections retained
static. `ImportedWallPhysics` implements manual gravity release, certified sleep,
static rubble publication, explicit rebuilding and failed-admission recovery.
Eight focused owner cases (seven protocol cases plus manual support removal),
14 topology tests and 13 exporter checks pass. The actual-house run exposed
coarse remainder collision intruding into released parts. The refined 5,921-box
bake and inscribed stud proxies eliminate that overlap and explosive release.
The three-brick GPU regressions now settle after contact coverage and cache
repairs. All 741 selected-part pairs pass the compiled-shape overlap audit. Eight
simultaneous normal patches preserve support alongside stud-side contact. At
16 solver substeps the synthetic wall certifies sleep in 717 ticks. The actual
house completes pure connection release in 52 ticks (supported pieces remain
standing), and explicit support removal in 72 ticks. Five old cell centres in the
surviving upper brick become vacant, excluding the removed brick itself. Both
variants certify static collision, pause safely, refuse damage saves and restore
original collision/saveability on Rebuild. Certification uses an owned zero-tick
capture and preserves the application's accumulator clock. Action 37 removes
source instance `415964676630277730` (3005), below surviving
`409735568841264200`; 38 source bindings remain, including other instances of the
same mesh. Action 35 releases connections without deleting a part; 36 rebuilds
all 39. This small upper-facade opening is NOT large enough for the player.
D2 stays open for complete publication refusal coverage, rendered/browser
acceptance, a genuinely walkable opening, normal hook and commit.
See `docs/validation/free-build/lego-destruction/d2-r01/`.

### D3 — cannon-triggered breakage

- [x] Route real certified GPU contact hits to accepted original source identities;
  repair sparse-history event loss and preserve dense-event/lifecycle regressions.
- [x] Add bounded connected-neighbour release and atomic projectile retirement
  with an explicitly approximate, dissipative transfer. Owner tests cover source
  preservation, current-pose re-impact, long support selection and packed-inertia
  torque/combined energy bounds. These do not complete coupled response below.
- [x] Add safe Visit cannon entry, one-outstanding-shot feedback and enforced
  shot/settlement Leave locks; keep manual release controls out of normal UI.
- [x] Correct near-zero physical separation in the real ground-wall shot and pass
  meaningful source-pivot motion checks without removing unrelated fixed collision.
  Actual-world result: 2.64 studs, 18.89 J, 136 ticks to settlement; rebuild restores collision.
- [ ] Complete the ordinary browser firing/rebuild journey and independent critique.

- [ ] Specify damage units, per-connection thresholds, impulse direction,
  local hit neighbourhood, accumulation/decay and deduplication. A neighbouring
  decoration must not detach merely because its bounding box overlaps the wall.
- [ ] Precompute bounded release candidates; reserve child shape/body/render
  capacity. Define impact-boundary activation and cancellation with GPU generations.
- [ ] Implement the coupled static-to-dynamic response described above. Remove
  the superseded parent contact; transfer momentum once. Preserve angular momentum
  using each released root's actual COM/inertia. Record intended dissipation.
- [ ] Test low/high/glancing impacts, anchored versus released mass, conservation
  within declared tolerance, static-to-dynamic switch, repeated impacts and
  simultaneous shots. For isolated ball/fragment tests check linear/angular
  momentum and the declared energy dissipation; for anchored structures include
  world/anchor impulses in the ledger. Avoid damage from resting contacts or old events.
- [ ] Verify a local hole and authentic GPU debris, with no invisible old wall
  or player/bike pass-through. Handle moving debris via the explicit query bridge.
- [ ] Gate: fire → local break → physical fragments → walk through hole;
  independent critique, focused tests, normal hook and commit.

### D4 — complete house and satisfying collapse

- [ ] Extend source-preserving hierarchy/connectors across the complete Blacksmith,
  including arbitrary-angle roof sections and intentionally independent objects.
- [ ] Support removal causes gravity-driven wall/roof collapse. Neighbours remain
  intact while their connections and supports survive. Tune impacts against a
  small set of repeatable shots, not random whole-building explosions.
- [ ] Repeated shots hit moving fragments reliably. Bodies sleep and contact
  stacks remain stable; settle-to-walkable-support transitions preserve geometry.
  Shoot settled rubble again, including with a player standing on it: wake must
  remove stale support and correctly transition the player to movement/falling.
- [ ] Measure actual native/browser p50/p95/p99 CPU and GPU frame times, physics
  cost, upload/readback, body/pair/contact/shape/event high-water and memory during
  repeated whole-house collapse. Record machine, adapter, settings and scenario.
- [ ] Fit approved performance targets by reducing physical grouping/detail and
  sharing assets before raising global caps. Report measured limitations plainly.
- [ ] Add restrained smoke, flash, recoil/camera motion and plastic impact sounds
  from authoritative events. Honour reduced-motion/audio preferences.
- [ ] Gate: purposeful before/impact/settled review, independent critique and
  hands-on owner feedback; normal required checks and commit.

### D5 — persistence and release

- [ ] Save source asset/version hash, accepted broken connections and part
  ownership, surviving roots and settled poses. Define handling of moving bodies
  and in-flight shots on save; never silently resurrect the original house.
- [ ] Explicit rebuild/reset restores the source model without deleting player
  construction. Conflicting saves retain existing player-construction precedence.
- [ ] Verify loading older saves, reset/leave/resource retirement, paused saves,
  no duplicate inventory and refusal on unsupported asset/schema versions.
- [ ] End-to-end native/browser regression: fire, collapse, inspect opening,
  walk/run/bike beside debris, build nearby, save/reload, reset and leave.
- [ ] Independent review; normal hook; commit. Push/deployment only when requested
  within the current publication scope. Record exact build and evidence.

## Validation locations

Extend `tests/test_gpu_authored_shapes.cpp`, `test_gpu_physics.cpp`,
`test_assembly_fracture.cpp`, `test_mesh_path.cpp` and
`test_adventure_runtime.cpp` where responsibilities match. Keep changed-behaviour
tests deterministic and meaningful. Add a dedicated imported-topology validator
and cannon/destruction integration scenario rather than stuffing all logic into
`Application`. Existing boat cut-tool tests do not prove automatic impact damage.

Store evidence under `docs/validation/free-build/lego-destruction/`. Run focused
checks during iteration, then the normal commit hook:
`bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`.
The preceding collision work found broader fixture/accounting test failures;
check their current state before claiming a gate commit. Do not bypass the hook.
Preserve unrelated work in this currently dirty checkout.
