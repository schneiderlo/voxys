# Voxys — build, explore, salvage: implementation TODO

Created: 2026-09-07. Source review baseline: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`.

**Purpose:** implement a complete, polished construction-and-salvage game using Voxys. This file contains the product requirements, architecture contracts, asset workflow, implementation order, and acceptance criteria. An implementing agent needs this file and the repository, not the originating conversation or a separate blueprint.

**Status:** implementation goal active from 2026-09-07. Checkboxes and the execution ledger record verified progress. Existing engine features are starting points, not completed expedition-game tasks.

## How an agent must use this file

- Read this entire file, root `AGENTS.md`, and root `README.md` before implementation. Recheck source when it differs from the baseline below. Repository paths in this file are relative to the repository root.
- Communicate in short, clear paragraphs. The project owner is highly dyslexic. Keep game UI text short and readable too.
- Work on an unchecked task whose prerequisites are complete. Claim its ID and file ownership in the execution ledger at the end before editing. Coordinate changes to shared schemas, build files, `Application`, and shaders.
- Independent asset, gameplay, rendering, and tooling work may run in parallel once their shared contracts are fixed. Give workers one bounded deliverable each. Do not let multiple workers overwrite one `.blend`, schema, or generated file.
- Preserve unrelated user changes. Existing LEGO, smooth terrain, RIDGEBREAK, and WRECKWATER routes remain regression targets. Add the new game through `?experience=salvage` and `--config salvage.cfg`; those are **new interfaces to implement**.
- The working defaults below resolve unspecified design choices. They are planning assumptions, not claims of separately approved commercial commitments. Use them unless later project instructions change them. Record any change in the decision ledger and update affected tasks before implementing it.
- Do not silently shrink the launch scope to a prototype, a two-client demo, or a collection of engine tests. Temporary milestone restrictions are named explicitly.
- Every checkbox requires its deliverable **and** its acceptance evidence. Use `docs/validation/salvage/<TASK-ID>/` for reports/captures, or record an equivalent repository evidence location in the ledger. Report actual executed commands, source revision, settings, expected/actual results, skips, failures, and remaining limitations.
- Mark `[x]` only after the task's tests and review pass. Use the ledger to mark in-progress/blocked work; leave the checkbox unchecked. Missing GPU hardware, artists, human testers, deployment access, or funding is not a passing result. Continue independent work when a gate is blocked.
- **User-required checkpoint rule:** after each gate G00–G14 passes, update its checkbox and evidence, run required repository checks, and create a Git commit for that gate's completed work. Record the commit in the ledger. Do not count a failed commit/check as a completed checkpoint, include unrelated changes, or bypass repository hooks. Gate commits are authorized; publication and purchases still follow the permissions below.
- Distinguish implemented, integrated, measured, and human-approved results. Concept art is not a gameplay screenshot; a synthetic tester is not a human playtest; software-driver correctness is not hardware performance.
- Update both Bazel and CMake integration for new shared code. Keep authoritative content identity, native/WASM packaging, and shader-source synchronization correct. Do not disable assertions or reduce the scene secretly to pass a gate.
- Tests should protect meaningful behavior: transactions, physics invariants, import correctness, real control journeys, save recovery, and performance. Avoid tests that only repeat implementation details.
- This file defines work and completion criteria. Actual external publishing, spending, installation, and account operations follow the permissions of the executing task. Do not invent credentials or assume a service is configured.

## Product contract — the game to build

**Player promise:** build a machine you care about, take it somewhere difficult, and bring something useful home.

**Loop:** choose a recovery job → modify a machine → travel to a wreck → detach/lift/tow/secure cargo → manage the loaded return trip → bank the haul → restore the harbor/unlock capabilities → revise the machine → take another job.

The memorable outcome is: **“My design mattered. I understood the problem. My adjustment worked.”** A well-designed craft may succeed on the first attempt. Do not script a capsize to force the intended story.

**Visual progress requirement, clarified 2026-09-07:** the owner found the first
cove preview substantially worse than the original prototype. BOOT-05's plain
boxes are a temporary lifecycle fixture, not an accepted art direction or a
visual-quality milestone. Preserve the original routes' appearance. Establish a
convincing small in-engine scene through LOOK-01 during the asset phase, before
expanding the game around unfinished visuals. Do not postpone the first serious
visual review until Phase 8. Engineering work with completed prerequisites can
continue in parallel; final art, rendering, animation and human gates still apply.

### Working launch scope

- Premium game for **Windows and Linux native**, with keyboard/mouse and controller parity. Keep a **desktop WebGPU playable demo and parity target**. Full browser campaign support is a gated expansion, not assumed. Console, macOS, and mobile releases are outside this plan's mandatory launch scope.
- Demo profile: the final-quality cove, the generator and awkward-crate jobs, the robot/boarding/recovery systems, and the 14 definitions in the initial catalog below including cutting and crane modules. Native builds can launch this same isolated demo profile and host 1–4 players with native/browser guests. Browser hosting is not required. Compare the selected profile's authoritative content identity; unrelated installed campaign assets do not cause a mismatch. Browser guests cannot join full-campaign sessions. Demo saves/progression are separate from campaign saves; only design blueprints can cross through validation, with no physical-resource transfer.
- Complete solo campaign, separate creative mode, and **1–4 player host-authoritative co-op**. Two-player co-op is an intermediate gate. Local solo play works offline and requires no remote game server.
- One original expressive utility robot as the player avatar. Third-person exploration and driving; orbit/pan/zoom workshop camera. **Walking on moving boats, boarding, falling overboard, swimming to safety, and rescue are required for launch.** The first proof may use helm/dock transitions.
- Four authored coastal regions. Target **50–70 functional part definitions** and **12–18 hours for a representative first campaign playthrough**. Paint colors, cosmetic duplicates, and simple size variants do not inflate the functional-part count. Human playthroughs establish duration, not mission arithmetic.
- Boats first. Powered cranes, structural cutting/repair, and amphibious haulers are required later families. Aircraft, submarines, general electrical programming, automated factories, combat, PvP, survival hunger/thirst, and fully excavatable terrain are excluded unless the product contract is revised.
- Environmental challenge comes from access, current/waves, weight, stability, terrain, structural loads, and damage. No enemy AI is required by this plan.
- Original construction-toy identity. Existing `lego` names identify legacy code; final content uses original characters, labels, and branding. No licensed character or logo is assumed.
- Landscape remains a heightfield with its shared brick-like surface. Interactive wrecks are explicit editable assemblies. Player machines are parts grouped into rigid clusters and joints. Arbitrary caves/overhang excavation requires separate volumetric work outside launch scope.

### Economy, recovery, and ownership

- Start with two resource classes: general salvage material and special machinery that unlocks a capability. Useful sidegrades survive later progression; a broad barge trades access/speed for load stability.
- Major build edits happen at workshops/service docks with a stable craft. Field actions initially adjust controls, release/reposition cargo, cut, patch, or replace explicitly permitted modules. Free welding during uncontrolled motion is excluded.
- A saved blueprint is a design. Duplicating a blueprint is free; spawning a physical copy consumes inventory unless it is the designated free starter recovery or creative mode.
- Starter restoration and rescue prevent an unrecoverable grind loop. Protect blueprints. Important lost cargo remains recoverable or receives a documented mission reset. Bound abandoned objects by region and significance; never silently delete unique progression cargo.
- Give the host world one durable starter entitlement. Its replenishable loan parts retain provenance and yield no bankable material or sale value. Replacement atomically retires or recovers earlier loan instances; paid additions stay owned and are recovered/stored through the rescue policy. Repeated valid rescue/dismantle requests cannot manufacture inventory. Damage/cutting never removes loan provenance.
- Solo pause stops simulation time. In co-op, a menu does not pause the shared world. Major editing requires a docked craft under an explicit edit lease; other players can continue elsewhere.
- Co-op world/progression/inventory belongs to the host save. Guests use session-scoped participation; cross-world inventory transfer is excluded. Personal blueprint export is allowed as design data only. Guest rewards update the host world once.
- Host departure ends the session after the last valid durable checkpoint or an explicit final save. No seamless host migration is promised. Disconnected continuous controls decay to a safe neutral state; committed one-shot operations remain exactly once.
- One region is active per session. Crews may take separate jobs inside it; travel to another region is a whole-party harbor transition. Require all crew and transferred craft/cargo in a secured staging state, release edit leases, settle pending transactions, and checkpoint before travel. Pin each attached/towed dependency group; no rope crosses a region boundary. Commit the destination baseline and manifest atomically; failed loading leaves the prior durable region recoverable. Persist valuable objects left behind under their recovery rules, without running off-screen regional physics.

### First two-job proof

**Job 1: stranded generator.** Start with a half-built skiff. Fit pontoons, engine, and winch. Recover a generator from a nearby wreck and return it to power the harbor lift. A high/off-center tow may overwhelm a narrow craft; a broad hull, low central tow, counterweight, cradle, or safer approach offers alternatives.

At the wreck, change cable length, approach, and cargo position. Return quickly to the workshop for pontoon/winch relocation. Test at least two viable strategies, including a first-attempt success.

**Job 2: awkward crate.** Reuse the same cove and mechanics with a heavier or differently mounted crate. The player modifies the craft and earns general material. This must be playable before testing voluntary repeat interest.

First proof: fixed starter chassis with editable module sockets, placeholder art, local thrust, fixed winch mast, real cargo, persistence, banking, rescue, and the second job. Fixed sockets simplify topology; they **do not** remove correct mass, center of mass, inertia, buoyancy, and force placement.

Final-quality slice: one polished harbor/cove/wreck; a 15–20 minute first session; about 12 initial part definitions; two starter designs; general connected construction; one controlled breakage/cutting case; powered crane; character/cameras; saved builds/cargo; final materials, water composition, sound, UI, and accessibility.

Initial catalog: beam, plate, pontoon, engine, propeller, helm, winch, tow eye, cargo cradle, brace, ballast, repair module. Add cutting and crane modules as their mechanics arrive. Each definition has explicit costs, mass properties, sockets, collision, and function.

### Art direction, including when the concept image is unavailable

Tactile slightly scuffed plastic, clearly assembled parts, readable connection points, restrained detail, broad color masses, warm cream/ochre craft, coral safety details, teal translucent water, and cool slate/warm stone coastal terraces. Soft coherent environmental light; strong landmarks visible from water level. Water behaves and looks like water, not literal plastic blocks.

Use silhouette and icons alongside color to distinguish flotation, structure, power, tools, and cargo. Preserve socket readability. The robot has a distinctive mechanical body and expressive face, not a branded minifigure silhouette.

A reference storyboard exists at `docs/concepts/build-explore-salvage.png`: workshop assembly → a listing crane boat recovering a generator → successful dusk homecoming. It is optional reference art, not a dependency or a shipped asset. The text above defines the direction without it.

## Current code facts — do not mistake these for the finished game

| Area | Existing foundation | Boundary/gap |
|---|---|---|
| Application | C++20 shared lifecycle; native/WASM entry points | `src/app/application.cpp` coordinates several prototypes; no expedition session yet |
| Main browser route | `web/index.html` defaults to `lego-world` | README contains older default-route prose; inspect source |
| Terrain | 8192² R16 heightfield, conservative max-height mips, hierarchical traversal, analytic columns/studs | Solid heightfield, not individually removable terrain bricks |
| LEGO geometry/layout | Plate pitch .32; stud radius .30; stud height .18; 32² chunk grouping | Ownership changes seams/color, not solid geometry |
| Layout cache | Fixed 1024² R32Uint toroidal texture, 4 MiB; max four chunk uploads/frame | Material ownership streaming, not terrain/content streaming |
| Playground | 48 bricks, 8 balls, fixed pad, three shapes, target challenge, 10 Hz bounded snapshots | No clutch bonds, vehicle modules, inventory, saves, or construction networking |
| Current stacking | Body height .96 + stud .18 = 1.14 spacing | Production connections need socket insertion and .32 lattice, not inherited loose stacking |
| GPU physics | Broad/narrow phase, solver, islands/sleep, CCD, queries/events, resident render poses | Shape API is primitives plus tagged box/stud special case; general shapes/inertia/joints are new work |
| Attachments | Powered tension-only distance rope with force/break limits | Does not supply hinge, suspension, piston, or general rigid latch |
| CPU backends | Box3D oracle and Jolt migration adapter | Not drop-in equivalents for LEGO terrain, authored compounds, events, transactions, lifecycle |
| Structural graph | Nodes/edges, damage ordering, components, mass/volume/COM | Fixture defaults 256 nodes/1,024 edges; no editable compound compiler; histories need bounds |
| Water | Two 256² FFT cascades plus long swells; GPU surface sampling | CPU approximation retains strongest 24 modes; not exact CPU/GPU parity |
| Composition | Terrain/water output precedes dynamic primitive/mesh drawing | Submerged dynamic objects lack correct refraction; water depth can reject them |
| Mesh content | GLB/glTF → VMESH, PBR texture slots, hierarchy, stored animation/skin data | Default mesh limits 256 instances/512 draws; shader does not yet execute a complete skeletal animation path |
| WRECKWATER | Native TCP graphical client, small authoritative world, replay/content contracts | Native bootstrap rejects WASM; authority scope is 3 main bodies + 8 fragments; not generic machine co-op |
| Determinism | Separate fixed-point fixtures | Production float GPU backend reports no cross-platform float determinism or lockstep |
| Performance | Stored visible hardware captures and software startup gates | Historical cached/offscreen FPS does not certify gameplay performance |

The existing WRECKWATER design is 12-player competitive salvage. This plan adds a separate expedition game; do not import its PvP/12-player requirements or erase its regression coverage.

## Architecture contracts — implement these consistently

### A. Units, identity, and time

- New assets use **metres, kilograms, seconds, radians**, right-handed canonical content coordinates with **+Y up, +X right, −Z authored forward**. Existing camera/projection code enables `GLM_FORCE_LEFT_HANDED`; keep canonical data independent and use explicit tested rendering/import bridges rather than changing legacy macros globally. Preserve existing terrain scale: one horizontal stud pitch = 1 engine unit = 1 metre for this game. Plate pitch .32 m; full brick body .96 m. These are stylized game dimensions, not real toy manufacturing dimensions.
- Author Blender meshes with applied unit scale, +Z up and −Y forward. Export/cook through one explicit basis conversion; test axes rather than applying a second guessed rotation. Quaternions in serialized game data are named `x,y,z,w`, normalized, with a canonical sign for hashing. Adapt GLM's constructor ordering explicitly.
- Canonical build placement uses an **isotropic .02 m integer lattice** on all three axes. One stud is 50 ticks, one plate 16, one brick body 48 and stud insertion 9. UI snapping exposes convenient stud/plate increments, while socket alignment may require finer offsets. The 24 proper axis-aligned rotations are exact signed-permutation matrices on this lattice. An anisotropic 1 m/.32 m translation grid is insufficient: a sideways plate moves its .32 m offset into X/Z. Keep occupancy sparse or analytic; do not allocate a dense .02 m voxel grid. The starter chassis can restrict permitted mounts. Engaged full-brick stacking advances .96 m; loose collision stacking is separate. Articulated root/joint motion remains continuous floating-point simulation, distinct from exact welded construction coordinates.
- Use durable asset/build/part/socket/connection/cargo/job IDs independent of GPU slots. A suitable initial runtime identity is a host-world namespace plus monotonically increasing 64-bit object counter; encode counters losslessly as strings/binary across JS. Reject exhaustion and duplicates.
- Keep `simulationTick`, `topologyRevision`, `authorityEpoch`, `requestSequence`, and generational `BodyHandle` separate. Commands name their expected revision and request identity. Serialization order must not depend on hash-map iteration or insertion order.
- Authoritative gameplay runs at fixed **60 Hz**. If physics substeps are used, their count/time are explicit. Render interpolation and browser RAF do not advance authoritative time independently. Every catch-up tick samples the correct water time.

### B. Canonical records and derived records

| Record | Required data |
|---|---|
| `PartDefinition` | Stable ID/version, permitted rotations/footprint, visual mesh/material/LODs, collision proxy, dry mass/local COM/inertia, buoyancy volumes, typed socket frames/occupancy, strength, module parameters, cost, salvage yield |
| `PartInstance` | Durable ID, definition ID/version, .02 m integer local placement/proper rotation, health, paint, configuration, owning build, paid/loan origin and starter-entitlement identity |
| `Connection` | Durable ID, endpoint part/socket IDs, weld/hinge/rope/etc. type, limits/strength/damage/enabled state |
| `BuildModel` | Durable build ID, revision, parts/connections/module settings, ownership/edit lease |
| `CargoRecord` | Durable ID, definition, mass/flotation, attachment/latch state, location/owner, mission role, recovery rule, value, banking receipt |
| `GameSession` | Accepted commands/receipts, fixed tick/epoch, inventory, jobs, discoveries, world changes, builds and cargo |
| `CompiledAssembly` | Derived root shapes, COM/inertia, joint anchors, buoyancy regions, part→root and contact→part mappings, content/topology hash; reconstructible, not canonical inventory |

Proposed new boundaries: `src/game/construction/{part_catalog,build_model,build_commands,assembly_compiler}.*`; `src/game/expedition/{game_session,expedition_state,salvage_inventory,save_store}.*`; `src/render/{assembly_path,scene_composition}.*`. These paths are proposals, not existing modules. Introduce them incrementally; keep GPU/render handles outside canonical game/save data.

### C. Commands and transactions

- UI previews query canonical occupancy/sockets immediately. Do not use 10 Hz debug readback as workshop authority. UI submits intent; only `GameSession` changes accepted topology, inventory, ownership, and missions.
- Receipts distinguish rejected, pending preparation, and committed. A repeated request within the retained receipt window returns the same outcome and cannot spend or bank twice. Persist contiguous processed-sequence high-water marks per admitted participant/session, retain pending requests, and reject expired older requests as stale/already processed after detailed receipts are compacted. Gaps enter a bounded reorder/resend policy; they cannot silently advance the mark. Retired session tokens never become valid again. Recheck expected revision at commit after asynchronous cooking.
- Persist banked-cargo/job-generation state or compact durable tombstones so evicting receipts does not make a reward executable again. Never reuse durable object IDs. Bound histories using checkpointed closed ranges/generations and explicitly retained active exceptions; do not accumulate one forever-growing vector of historical receipts or tombstones.
- Reserve the worst permitted resources before commit: parts, bodies, child shapes, joints, contacts/events as applicable, render mappings, journal/staging space. Keep the old revision valid while preparing; discard obsolete/canceled work safely.
- Commit graph, shapes, mass/inertia, body/joint endpoints, collision filters, render mapping, ownership, and journal under one revision. Never backdate a mutation into GPU ticks already submitted. Define a bounded completed-evidence frontier and pending queue.
- Old shape, body, draw and mapping resources remain valid until the last referencing GPU submission completes. Use generation/content-tagged resources and bounded fence-based retirement from the first live rebuild. A committed replacement cannot immediately recycle memory still read by rendering, queries, or simulation; later world streaming reuses this mechanism.
- Undo/redo are compensating transactions with inventory accounting. Co-op undo cannot erase another participant's later work.
- Initial cargo states: world-loose → towed (independent body + rope) → latched (rigidly incorporated into a compiled cluster) → released/world-loose or banked. Other valid implementations require an explicit contract revision. Never count cargo mass both in the hull and an independent body. Attach/release preserves pose, correct momentum, collision filtering, and ownership.
- Latching requires compatible cargo/cradle sockets and authoritative position/orientation/relative-motion checks. Start capture tuning at ≤.10 m socket separation, ≤5° angular misalignment, ≤.5 m/s relative socket speed and ≤30°/s relative angular speed; data may refine these after measured tests. Do not teleport distant cargo. Compute the merged body's `P=Σmᵢvᵢ`, `v=P/M`, and angular momentum about merged COM `L=Σ[Iᵢ_world ωᵢ+(cᵢ−C)×mᵢvᵢ]`, then `ω=I_merged_world⁻¹L`. This is controlled inelastic capture: dissipated kinetic energy is expected, spontaneous energy creation is not. Small alignment corrections must respect the capture envelope and documented momentum/energy policy. Release inherits the cluster velocity field at each resulting COM.
- Durable banking writes its accepted journal transaction before displaying a durable success receipt. A later checkpoint failure must not lose an acknowledged reward. Quota/full-disk failures produce a recoverable unsaved state and no false durable acknowledgment.

### D. Mechanics and conservation

- Welded connected parts form one rigid cluster. Create separate bodies for actual relative motion, cargo, loose objects, and significant disconnected fragments. Render parts from `rootPose × partLocalPose`.
- For mass `mᵢ`, part COM `cᵢ`, local inertia `Iᵢ`, and orientation `Rᵢ`: `M=Σmᵢ`, `C=Σ(mᵢcᵢ)/M`, `rᵢ=cᵢ−C`, `I=Σ[RᵢIᵢRᵢᵀ + mᵢ((rᵢ·rᵢ)Identity−rᵢrᵢᵀ)]`. Validate finite positive mass and physically valid inertia. Use full tensors, or a documented stable principal-axis decomposition with sign/order/degeneracy handling.
- Compile simplified exterior collision proxies and child acceleration. Do not use every decorative stud as a vehicle contact. Logical socket bonds supply connectivity; internal mating surfaces are removed from assembled collision.
- Apply buoyancy at submerged-volume centers: magnitude `ρgV`; torque `r×F`. Drag uses velocity relative to local water, including angular velocity at the sample. Thrust acts at the propeller point. Do not double-count overlapping volume.
- `WaterField` identifies tick, spectrum/seed, frame, height, normal, and velocity. Compute forces from the state for that tick without a CPU-readback dependency cycle. Define CPU approximation errors explicitly; render interpolation is presentation only.
- Welded internal bonds have no joint reaction impulse available. Structural stress uses a bounded documented load-distribution approximation; do not pretend to have full finite-element stress.
- Broken components inherit the parent's **post-solve** velocity field: `v_fragment=v_parent+ω_parent×r`, `ω_fragment=ω_parent`. Do not apply the breaking impact twice. Reserve significant-fragment capacity before breaking; preserve unique cargo/functional parts under pressure.
- One authoritative body has one solver owner. If a CPU machine path wins a later benchmark, adapt it explicitly and use compact pose uploads. Cosmetic GPU debris does not push authoritative CPU craft without a separately implemented coupling.

### E. Saves and co-op consistency

- Save schema/content versions, durable IDs, topology, root poses/velocities, joint and motor state, rope lengths, cargo/latch ownership, damage/flooding, module resources, inventory, jobs/discoveries, water/simulation time/seeds, and journal sequence.
- Capture one certified completed simulation tick. Do not combine current topology with old poses or incomplete events. Initial save guarantee is **semantic resume** with controlled settling, not bit-exact continuation. Exact continuation needs additional compatible solver/contact history and separate certification.
- Native: crash-safe checkpoint replacement plus bounded journal and backup. Browser: transactional IndexedDB plus export/import and quota handling. Never persist raw GPU buffers or transient handles as canonical saves.
- Float GPU replay needs authoritative checkpoints/outcomes, not input-only cross-device lockstep. Reuse fixed-point fixture algorithms selectively; they do not certify the production float world.
- Reliable ordered channels carry topology/economy/session events; bounded sequenced snapshots carry motion. Every snapshot names authority epoch, simulation tick, and topology revision. Unknown topology enters a bounded wait queue and requests a baseline. Show a revision only when compatible mappings and poses are ready.
- Replication interest retains all relevant rope endpoints, cargo, and connected machinery. Start testing with 20 Hz root snapshots over 60 Hz simulation; tune from measurements. Cross-channel ordering is not assumed.

### F. Rendering and resource ownership

Target order: fixed simulation/water → visibility and shadow inputs → terrain and dynamic opaque HDR/depth → water/refraction → ordered underwater/transparency/effects → temporal reconstruction → one exposure/tone/output transform → UI.

Keep sampled opaque HDR/depth immutable while water writes separate attachments; avoid sampled-attachment feedback. Keep static terrain visibility/material caches, but invalidate or composite moving shadows independently. Add dry-hull water exclusion and flooding-aware interior water.

Use root/local-part instance mappings, root-first culling, part-cluster LOD, mesh/material bins, indexed indirect draws, and topology/material delta uploads. The GPU path reads resident root poses; a deliberate CPU path has bounded root uploads.

Temporal work needs previous/current camera/root poses, motion vectors, depth/ID/topology rejection, water/foam reactive handling, and sector-rebase handling. Separate unjittered cache identity from jitter. Either implement valid cached visibility reconstruction or budget retracing; do not silently invalidate the terrain cache every frame.

Feature-gate subgroup optimizations. Do not assume a fixed SIMD width or mapping to local invocation indices. Profile bandwidth, registers/spills, dispatches, barriers, and full-frame time. High-degree constraints affect scheduling; long chains/loops and mass ratios stress convergence. Occupancy alone is not a performance goal.

## Capacity, performance, and human gates

These numbers are **initial design budgets requiring implementation and measurement**, not existing proven capacities. Unless noted, counts are total loaded-world counts, not per player.

| Resource | Initial budget |
|---|---:|
| Logical parts per player craft | 256 |
| Significant rigid roots, including links/cargo/fragments | 64 |
| Active mechanical joints, including ropes | 128 |
| Detailed visible part instances | 8,192 |
| Significant salvage items | 32 |
| Resident edited structures | 8 |
| Cosmetic debris | 512 pooled, lifetime/distance limited |

Define independent caps for candidate pairs, compound child pairs, contacts/manifolds, commands, events, pending mutations, topology history, snapshots, readbacks, uploads, and saves. Root count alone does not bound these. Use checked arithmetic for allocation sizes. On capacity pressure, reject/defer a request explicitly or use a predefined meaningful coarse representation; never silently lose contacts, cargo, or accepted edits.

Start with the known Radeon 890M as a reproducible low-end engineering reference. Before release, declare exact AMD/Intel/NVIDIA machines, CPU/RAM, drivers, power modes, display rates, resolution, and presets. Native Windows/Linux and the supported desktop-browser demo need real visible runs.

| Full GPU-frame planning allocation at 1080p | Ceiling |
|---|---:|
| Machine/contact simulation | 3.0 ms |
| Terrain/static work | 2.5 ms |
| Dynamic geometry/shadows | 2.5 ms |
| Ocean simulation/composition | 2.0 ms |
| Effects/temporal/output | 2.0 ms |
| Contingency | 1.5 ms |
| **Total** | **13.5 ms** |

Target CPU gameplay/encoding below 4 ms p95 on the named reference. CPU/GPU overlap must be measured, not assumed. Multiple fixed ticks required in one displayed frame must be included.

Product targets: fixed 60 Hz simulation; p95 presented cadence within 16.67 ms under a declared measurement tolerance; p99 below 25 ms; no topology/streaming hitch above 50 ms in the scripted slice route. These are proposed gates. Change thresholds only through a recorded contract revision with reasons, not after a failing run to hide it. Record frame misses, queue age, and input latency separately.

Stored 2026-09-07 evidence on Radeon 890M, Chrome 152, Linux/Wayland, 1920×1080: walking GPU p50/p95 6.69/7.54 ms and RAF cadence 17.70/19.30 ms; 48 bricks + 8 balls GPU 4.36/4.60 ms and cadence 18.70/23.30 ms. These are short earlier captures, not results of this implementation. RAF cadence is not direct photon latency. Historical ~945 FPS cached/offscreen renderer throughput is not the product gate.

Stored live WASM startup allocation is approximately 404.2 MiB of a fixed 512 MiB heap. GPU memory is separate. One 8192² R16 image is 128 MiB; its full same-format mip chain is approximately 170.7 MiB. The 4 MiB material atlas does not mean the world is memory-cheap. Measure transient peaks, staging, and resources pending GPU retirement.

Initial human gate: 8–12 actual participants, including construction-game newcomers. Targets: ≥80% launch within three minutes without spoken help; ≥70% finish a haul unaided; ≥60% deliberately improve a design and explain its effect; ≥50% voluntarily start the second job; zero critical save/progression failures. Record denominators, observations, blockers, and accessibility issues. This diagnostic sample does not establish commercial retention.

## Execution map

| Milestone | Required gates | Meaning |
|---|---|---|
| Baseline and contracts | G00, G01 | Reproducible starting point and compatible interfaces |
| Asset pipeline and early visual proof | G02, including LOOK-01 | Authored models and gameplay metadata work in engine; a small real scene demonstrates the intended look |
| First two-job playable proof | G03, G04, G05, G06 | Correct craft physics, durable state, usable water composition, actual repeated loop |
| Mechanical foundation | G07 | General assemblies, articulation, fracture, complete checkpoints |
| Final-quality slice | G08 | Finished visual/audio/input/character experience in one cove |
| Production tools and streaming | G09 | Designers can add content; memory and loading are bounded |
| Co-op proof then launch count | G10, G11 | Two real clients, then four real clients |
| Full game content | G12 | Four regions, functional part library, campaign and creative mode |
| Release candidate | G13 | Hardware, reliability, accessibility, and human gates |
| Release | G14 | Versioned packages, operational support, release evidence |

Each task below states **Needs**, **Deliver**, and **Pass**. Evidence destination is the task-ID directory defined above. Gates depend on all listed work; a gate is not satisfied by a screenshot alone.

## Phase 0 — baseline, scope, and collaboration

- [x] **BOOT-01 — Establish the actual checkout baseline.**  
  **Needs:** none. **Deliver:** source revision, dirty-file inventory, applicable instructions, existing routes, current build configuration, and source differences from this file's baseline. **Pass:** a second agent can reproduce the environment without overwriting existing work; stale documentation is identified.

- [x] **BOOT-02 — Make build and test commands reproducible.**  
  **Needs:** BOOT-01. **Deliver:** working Nix/toolchain setup, native/WASM builds, focused baseline tests, and exact commands/tool versions in the report. **Pass:** failures/skips are recorded honestly; both build systems include shared modules; existing scene assets match their sources. Do not claim a full suite from a selected test subset.

- [x] **BOOT-03 — Record visible rendering and memory baselines.**  
  **Needs:** BOOT-02. **Deliver:** walking, shore, full playground, cold startup, steady-state timing, and separate CPU/WASM/JS/GPU live/peak measurements. **Pass:** device/driver/display/power settings and screenshots accompany completed GPU timing; cached throughput is labeled separately.

- [x] **BOOT-04 — Publish the working product and release matrix.**  
  **Needs:** BOOT-01. **Deliver:** confirm this file's launch defaults, demo/campaign compatibility and whole-party regional travel in the decision ledger; assign feature/asset/QA owners and specify Windows/Linux/browser validation machines. **Pass:** every mandatory launch family, session profile and deferred platform has an explicit status. Routine implementation can proceed on the written defaults; missing funding/platform hardware is recorded, not guessed.

- [x] **BOOT-05 — Add a separate expedition launch path.**  
  **Needs:** BOOT-02, BOOT-04. **Deliver:** `salvage.cfg`, browser `experience=salvage`, a placeholder authored cove, clean game-state lifecycle, and packaging/build entries. **Pass:** all old routes still launch; the new mode initializes/resets/exits without leftover physics objects or UI state.

- [x] **G00 — Baseline ready.**  
  **Needs:** BOOT-01, BOOT-02, BOOT-03, BOOT-04, BOOT-05. **Pass:** an independent agent can launch old/new modes, find known failures, and begin a task without the conversation.

## Phase 1 — game data and command authority

- [x] **DATA-01 — Encode units, frames, IDs, and canonical serialization.**
  **Needs:** BOOT-04. **Deliver:** Contract A types/converters, isotropic integer placements/proper rotations, stable ID allocation, sorted canonical encoding, lossless JS exchange, and explicit schema version. **Pass:** all 24 rotation compositions preserve lattice/socket placement; sideways/upside-down connections, axis/quaternion fixtures, negative coordinates, 64-bit IDs, exhaustion rejection and insertion-order independence pass.

- [x] **DATA-02 — Implement the minimum immutable part catalog.**  
  **Needs:** DATA-01. **Deliver:** Contract B definitions for the starter catalog, module parameters, typed socket frames, authoring-validation errors, and content lookup. **Pass:** malformed mass, missing assets, incompatible sockets, invalid rotations, and duplicate definitions fail clearly; valid catalog loads without a renderer.

- [ ] **DATA-03 — Implement canonical builds and occupancy.**  
  **Needs:** DATA-02. **Deliver:** persistent parts/connections, fine-lattice placement with coarse UI increments, stable build revisions, sparse/analytic socket and solid occupancy, overlap rules, provenance and blueprint duplication. **Pass:** edit/encode/decode round trips preserve data; sideways/asymmetric connections fit exactly; full-brick engaged spacing is .96, not loose-stack 1.14; duplication creates no free physical inventory and dense tiny-cell allocation is avoided.

- [ ] **DATA-04 — Introduce local GameSession with a fake physics adapter.**  
  **Needs:** DATA-03, BOOT-05. **Deliver:** sole authority for commands, inventory, cargo, mission state and receipts, independent of rendering. **Pass:** duplicate/stale/invalid requests, canceled preparation, insufficient capacity, and failed edits leave canonical state unchanged; direct UI state mutation is removed from this mode.

- [ ] **DATA-05 — Implement transaction preparation, commit, and compensation.**  
  **Needs:** DATA-04. **Deliver:** pending/rejected/committed receipts, bounded receipt retention and durable processed-sequence markers, resource reservations, revision revalidation, cancel/discard, undo/redo and inventory accounting. **Pass:** delayed preparation cannot overwrite newer state; retries spanning receipt eviction remain stale/idempotent; gaps and retired tokens cannot bypass ordering; repeated/compensating operations create no value and the old revision remains usable until commit.

- [ ] **DATA-06 — Define bounded event and telemetry schemas.**  
  **Needs:** DATA-04. **Deliver:** stable typed events for commands, forces, attachment state, damage, cargo, rewards, UI/audio, and diagnostics; bounded queues and explicit overflow counters. **Pass:** consumers cannot mutate authority; slow consumers do not grow memory without bound or lose economic transactions.

- [ ] **G01 — Canonical contracts ready.**  
  **Needs:** DATA-01, DATA-02, DATA-03, DATA-04, DATA-05, DATA-06. **Pass:** headless tests build/edit/undo/serialize a starter design without GPU or UI dependencies and preserve exact inventory semantics.

## Phase 2 — Blender and asset production

### Tool facts and rules for asset agents

Blender **5.2.1 LTS** was verified on the original host with a headless `bpy` session. It is installed through `/snap/bin/blender`; the Snap launcher needed normal host permissions outside the restricted workspace sandbox. That test establishes Blender/Python availability, not asset-export correctness. Recheck the executable/version on another host; do not bypass its security mechanisms.

The owner is interested in Blender MCP. Use a functioning Blender MCP when available and useful for scene inspection/interactive iteration. Evaluate its actual commands and document the result. Its absence must not block reproducible `bpy` generation. Preserve scripts, parameters, `.blend` sources, exports, and renders whichever control route is used.

Current agents can run Blender directly. Separate headless Codex workers are optional batch orchestration: `codex exec` is the non-interactive command; `-p` selects a profile. Each worker reads this file, receives an asset ID/specification and isolated output directory, and must render/validate its work. Do not launch unbounded workers or call file creation alone a finished asset.

Image generation may supply reference art, base-color artwork, decals, wear masks, and texture drafts. Use neutral illumination for base color; verify tiling, seams, UV placement, and the absence of painted lighting. Generate/bake coherent normal/roughness/metalness separately; plausible-looking image maps are not automatically physically correct.

The built-in image tool chooses its model internally; never falsely label its output “Image 2.” If **exact `gpt-image-2`** selection is required, use a configured supported Image API/official skill workflow and record the actual model; API access is separate from an installed Blender. Do not invent credentials or switch model silently. Store prompts, references, actual provider/model when exposed, and outputs.

Current export profile: triangulated glTF/GLB; UV0 only; base color, tangent-space normal, metallic/roughness, emissive; metallic in blue, roughness in green; color/emissive sRGB, data maps linear. VMESH embeds decoded RGBA8, not compressed KTX2. Separate AO has no current image slot; do not count on it being rendered. Bake Blender node graphs into supported maps/factors or explicitly implement equivalent runtime shading.

Current converter stores TRS animation with LINEAR/STEP and four joint influences; cubic splines, morph animation, and other features are not a ready runtime path. The mesh shader currently uses the instance transform, so stored skin/animation fields do not imply working animation. Some unsupported glTF features may be silently ignored: add validation before relying on them.

- [x] **ASSET-01 — Establish the authoring/control workflow.**  
  **Needs:** DATA-01, BOOT-02. **Deliver:** versioned Blender scripts/export preset, optional MCP assessment, per-asset output conventions, parameters/seeds, and smoke renders. **Pass:** a clean headless run regenerates one test asset; any MCP-assisted edits can be saved/reproduced; no existing scene is overwritten accidentally.

- [ ] **ASSET-02 — Add gameplay metadata beside render assets.**  
  **Needs:** DATA-02, ASSET-01. **Deliver:** versioned sidecar bound to the GLB content digest for stable sockets, collision proxies, buoyancy volumes, mass properties, LODs and tool anchors. **Pass:** metadata survives export/cook; object ordering/array indices are not durable IDs; stale sidecars are rejected.

- [ ] **ASSET-03 — Enforce a supported glTF profile.**  
  **Needs:** ASSET-02. **Deliver:** explicit validation for unsupported required extensions, sparse accessors, extra UVs/influences, morphs, vertex colors, texture transforms, primitive modes, nonfinite transforms and limits. Validate every vertex attribute's count against POSITION before conversion reads it. Correct the observed matrix rotation reversal in `decomposeMatrix` and test equivalent matrix/TRS nodes, including asymmetric rotations, before accepting matrix-authored assets. **Pass:** supported golden assets survive; malformed attribute counts and unsupported required appearance/function fail loudly instead of disappearing or causing out-of-bounds reads; matrix/TRS equivalence and canonical-frame fixtures pass. Only collision/flotation volumes that need closure must be watertight. The LOOK-01 preliminary investigation identifies these pre-existing importer gaps; they are not BOOT-05 regressions.

- [ ] **ASSET-04 — Validate one pontoon through the complete pipeline.**  
  **Needs:** ASSET-03. **Deliver:** parameterized `.blend`, GLB, sidecar, VMESH, LODs, material maps, thumbnail, turntable, socket/volume dimensions, and provenance. **Pass:** native and WASM show correct scale, axes, winding, material, socket fit and bounds in the same test scene; independent technical review validates the pipeline. Simple prototype visuals are sufficient here; final visual approval belongs to VIS-03/G08.

- [ ] **ASSET-05 — Create the first functional asset kit.**  
  **Needs:** ASSET-04. **Deliver:** beam/plate, propulsion/tool module, helm, fixed winch, cradle, generator and follow-up crate using the same conventions. **Pass:** both starter designs assemble correctly in an asset/socket fixture; contact/attachment/buoyancy metadata validates. Runtime physics acceptance follows in G03; this task must not depend on later live compound support. Prototype art is allowed.

- [ ] **ASSET-06 — Establish material and texture standards.**  
  **Needs:** ASSET-04. **Deliver:** shared plastic/metal/rubber/glass response, limited palette, decal/trim usage, bake/image-generation recipe, texel-density and texture-memory limits. **Pass:** neutral/light/dark/wet previews remain coherent in engine; generated color textures have no unwanted fixed shadows; normals/roughness respond correctly to changing light.

- [ ] **ASSET-07 — Add asset build, provenance, and review records.**  
  **Needs:** ASSET-03, ASSET-06. **Deliver:** deterministic asset manifest including source/cooker versions, content hashes, references/prompts, dependencies, ownership/license information, and review status. **Pass:** a clean build reproduces the declared inputs or records known nondeterministic steps; required placeholder/missing/unapproved assets are reported.

- [ ] **LOOK-01 — Prove the visual direction early in the real engine.**  
  **Needs:** BOOT-05, ASSET-05, ASSET-06. **Deliver:** one deliberately composed, walkable section of the cove with a recognizable modular skiff, dock and generator, using the authored asset kit. Establish tactile plastic/metal materials, readable bevels and connections, balanced lighting, useful foreground/mid-distance/horizon and a clear starting camera. A stationary craft is permitted at this milestone; label its simulation status. **Pass:** inspect actual native and browser screenshots plus a short moving-camera capture at declared matching resolution/settings. Compare with the original prototype and the textual art direction; reject large raw placeholder boxes as the main vessel, featureless flat surfaces, unreadable silhouettes, obvious intersections and a degraded terrain/water backdrop. Independent visual review must identify what improved and any remaining composition/rendering defects. Retain the owner's feedback and do not equate an agent's review with owner approval. An offline Blender render or generated concept cannot pass this task. Visible water/opaque ordering defects must be fixed or keep this task pending; REND-01–04 remain responsible for the complete dynamic composition contract. This is the next visual milestone, before broad content production; it does not replace final VIS-03/VIS-05/G08 approval.

- [ ] **G02 — The asset pipeline and early visual direction are proven.**  
  **Needs:** ASSET-01, ASSET-02, ASSET-03, ASSET-04, ASSET-05, ASSET-06, ASSET-07, LOOK-01. **Pass:** a designer changes a pontoon parameter, rebuilds, and sees the correct native/browser result without editing C++ or WGSL; the real small cove scene passes LOOK-01 visual review.

## Phase 3 — correct starter-machine simulation

- [ ] **SIM-01 — Build an analytical assembly compiler.**  
  **Needs:** DATA-03, ASSET-02. **Deliver:** welded-component detection, exterior collision proxies, mass/COM/inertia, buoyancy regions, module/joint frames, stable contact→part and part→root mappings, and cache identity. **Pass:** independent asymmetric fixtures validate the parallel-axis formula; ballast relocation changes COM; equivalent insertion orders produce equivalent outputs; invalid/over-limit input is rejected.

- [ ] **SIM-02 — Extend physics shape and mass contracts.**  
  **Needs:** SIM-01. **Deliver:** authored compound/shape identity independent of material flags, local COM/full inertia support, and a documented body/build frame transform. Start with the bounded starter hull profile. **Pass:** pose integration and force-at-point torque agree with analytical tests; legacy primitive/studded-brick behavior remains valid; optional CPU capabilities fail explicitly if unsupported.

- [ ] **SIM-03 — Carry compounds through every physics consumer.**  
  **Needs:** SIM-02. **Deliver:** correct bounds, narrow phase, terrain contacts, CCD, ray/sweep queries, events, mass use, render/debug mappings, and generation-safe lifetime. **Pass:** the same asymmetric compound settles, collides, sweeps, and renders correctly; sockets/contacts map to the correct part after handle reuse.

- [ ] **SIM-04 — Establish fixed-tick scheduling and the GPU frontier.**  
  **Needs:** DATA-04, SIM-03. **Deliver:** explicit scheduled/submitted/completed ticks, bounded ticks in flight, complete pose/event evidence, timeout/error behavior, pause/catch-up policy, and future mutation scheduling. **Pass:** rendering at 30/60/144 Hz does not change accepted tick order; no mutation is applied to an already submitted tick; device stalls do not produce false completed state.

- [ ] **SIM-05 — Implement the authoritative WaterField.**  
  **Needs:** SIM-04. **Deliver:** tick/spectrum/seed/frame contract, GPU height/normal/velocity sampling, named CPU approximation with error tolerance, and presentation interpolation. **Pass:** multiple catch-up ticks use their own sea state; forces do not require reading back tick N to populate tick N; solo pause freezes gameplay waves consistently.

- [ ] **SIM-06 — Bind prepared build transactions to physics.**  
  **Needs:** DATA-05, SIM-03, SIM-04. **Deliver:** atomic prepare/commit/discard across shapes, bodies, filters, mappings, ownership and revisions, plus generation-safe fence retirement for old resources. **Pass:** concurrent edit/cancel/rebuild with delayed GPU completion leaves referenced revisions/resources valid; stale preparations, early reuse and capacity failures cannot partially spend inventory, alter collision or expose retired mappings.

- [ ] **SIM-07 — Make module placement affect the boat.**  
  **Needs:** SIM-05, SIM-06. **Deliver:** bounded submersion-based buoyancy, water-relative drag, engine/propeller forces, steering/thrust controls and configured module limits. **Pass:** offset loads change roll/trim; a wider flotation arrangement resists the same load better; propeller relocation changes torque; overlapping volumes do not multiply flotation.

- [ ] **SIM-08 — Complete winch and cargo-restraint behavior.**  
  **Needs:** SIM-07, DATA-06. **Deliver:** selecting valid tow eyes, attach/reel/pay-out/release, tension/overload events, capture-envelope validation, cradle latch via the prepared compound transaction, and combined-momentum merge/unlatch/filter restoration. **Pass:** analytical asymmetric two-body merge/release fixtures conserve mass and momentum without creating kinetic energy; distant/fast/misaligned cargo is rejected; slack-to-taut and high-load tests remain stable and cargo has one mass representation and durable identity through repeated cycles.

- [ ] **SIM-09 — Bound mixed-size collision and awake work.**  
  **Needs:** SIM-03, SIM-08. **Deliver:** measured large-object path, compound-child acceleration, contact budgets, active/sleeping compaction and wake propagation. The existing oversized-body all-resident scan is the baseline to compare. **Pass:** dense craft/cargo/debris tests report bounded candidates and zero silent contact loss; sleeping systems wake under engine, rope, contact, or edits.

- [ ] **G03 — Starter mechanics are physically meaningful.**  
  **Needs:** SIM-01, SIM-02, SIM-03, SIM-04, SIM-05, SIM-06, SIM-07, SIM-08, SIM-09. **Pass:** two differently configured hulls show understandable empty/loaded handling; rope/latch operations preserve identity and mass; physics evidence covers native and WASM GPU paths.

## Phase 4 — minimal durable progress, before the first playtest

- [ ] **SAVE-01 — Specify versioned save and journal formats.**  
  **Needs:** DATA-05. **Deliver:** canonical blueprint/inventory/job/cargo schema, checksums, processed-sequence markers, bounded reward tombstones/generations, compatibility policy, migration entry points and journal compaction. **Pass:** canonical encode/decode, unknown-version rejection, duplicate-ID detection and corrupt/truncated records are tested without the renderer; replaying an old request after compaction and reload cannot spend or reward again.

- [ ] **SAVE-02 — Implement native durability and backup recovery.**  
  **Needs:** SAVE-01, BOOT-02. **Deliver:** platform save directories, staged writes, correct flush/replace sequence, journal replay, last-good backup, and errors for full disk/permissions. **Pass:** interruption before/after each edit/bank/undo boundary cannot duplicate inventory or lose an acknowledged durable banking receipt.

- [ ] **SAVE-03 — Implement browser persistence and export/import.**  
  **Needs:** SAVE-01, BOOT-05. **Deliver:** IndexedDB transactions, bounded write queues, quota/error reporting, save backup and user export/import. **Pass:** reload/interruption/quota/corruption cases preserve the last good state; exported IDs/counters are lossless; imports validate before replacing any current save.

- [ ] **SAVE-04 — Capture certified starter-expedition state.**  
  **Needs:** SAVE-02, SAVE-03, SIM-08, SIM-04. **Deliver:** checkpoint one completed tick including build/cargo motion, latch/rope targets, module state, inventory/jobs, water time and journal sequence. **Pass:** save/load during towing, after latching, after release and after banking restores the same meaningful state without double mass or rewards; semantic-resume settling is controlled.

- [ ] **G04 — The first proof cannot lose progress silently.**  
  **Needs:** SAVE-01, SAVE-02, SAVE-03, SAVE-04. **Pass:** native/browser recovery fixtures pass; banking acknowledgment and checkpoint semantics are documented in the report and reflected in UI state.

## Phase 5 — coherent scene composition

- [ ] **REND-01 — Define frame resources and pass contracts.**  
  **Needs:** BOOT-05, ASSET-05. **Deliver:** explicit HDR color/depth formats, linear/world depth conversion, exposure ownership, static cache keys, render-target lifetime and synchronization. **Pass:** a test boat/cargo scene has a documented resource graph; no sampled-attachment feedback or duplicated output conversion.

- [ ] **REND-02 — Draw dynamic opaque objects before water.**  
  **Needs:** REND-01, SIM-03. **Deliver:** terrain plus dynamic machines/cargo/character into common opaque HDR/depth; immutable sampled inputs and separate water output attachments. **Pass:** submerged cargo is visible through refraction; foreground occlusion and above/below-water crossings are correct; WebGPU validation is clean.

- [ ] **REND-03 — Unify lighting, shadows, and hull interiors.**  
  **Needs:** REND-02. **Deliver:** shared environment/direct lighting, moving-object shadows, static-cache separation, dry-hull water exclusion and flooding-ready interior-water interface. **Pass:** a stationary camera sees moving shadows update; dry open boats contain no ocean plane; object/terrain/water exposure agrees.

- [ ] **REND-04 — Specify transparency and medium composition.**  
  **Needs:** REND-03. **Deliver:** ordered ropes/glass/spray/particles/underwater medium, depth sampling rules and bounded reflection fallback. **Pass:** cable, window, splash and submerged-part test cases remain readable; off-screen reflection limitations are documented instead of hidden.

- [ ] **G05 — Water and machinery form one visible world.**  
  **Needs:** REND-01, REND-02, REND-03, REND-04. **Pass:** native/browser test captures show correct underwater salvage, shadows, hull interior and output color through the actual application.

## Phase 6 — first two-job game, using real controls

- [ ] **PLAY-01 — Author the cove and job state machine.**  
  **Needs:** BOOT-05, DATA-04, ASSET-05. **Deliver:** workshop, launch/recovery spots, route landmarks, wreck, generator, crate, harbor extraction, and data-driven states: available/accepted/launched/recovery/return/banked plus abort/rescue/retry. **Pass:** predicates use authoritative state; no mission script teleports cargo or injects success to replace the mechanic.

- [ ] **PLAY-02 — Add input actions and a minimum builder.**  
  **Needs:** DATA-05, BOOT-05. **Deliver:** keyboard/mouse/controller action map, socket/ghost preview, valid/invalid feedback, select/add/remove/move/rotate/configure, undo, save/name/duplicate, camera focus/orbit/pan/zoom. **Pass:** both input schemes modify a starter hull through commands; UI focus/pointer capture cannot accidentally trigger construction or propulsion.

- [ ] **PLAY-03 — Implement launch, helm, towing, and latching UI.**  
  **Needs:** PLAY-02, SIM-08, PLAY-01. **Deliver:** dock/helm transition, chase view, target selection, reel/pay-out, tension feedback, release and confirmed cradle latch. **Pass:** a player performs each action without debug controls; controller-only use is possible; the loaded return route is visibly and physically different.

- [ ] **PLAY-04 — Implement banking and visible harbor reward.**  
  **Needs:** PLAY-03, SAVE-04. **Deliver:** generator banking transaction, lift-power harbor state, capability unlock, general material rewards and persistent job receipts. **Pass:** repeated extraction triggers/reloads/requests cannot pay twice; the world visibly changes only after a committed reward.

- [ ] **PLAY-05 — Implement rescue and protected recovery.**  
  **Needs:** PLAY-04. **Deliver:** abort/rescue path, safe spawn, durable starter entitlement and loan provenance, paid-addition recovery, retained blueprints, unique-cargo recovery/reset rules and bounded abandoned objects. **Pass:** loss at every first-job stage is recoverable; repeated rescue→dismantle→bank→restore cycles create no free value, including after cutting or reload; rescue does not duplicate cargo or erase the only progression item; repair/return options are clear.

- [ ] **PLAY-06 — Make the second job and sidegrades useful.**  
  **Needs:** PLAY-05. **Deliver:** heavier/awkward crate job, altered tow/latch challenge, two valid build/route strategies and an immediately useful upgrade. **Pass:** both jobs can be completed without one mandated blueprint; a stable first design is not forced to fail; reward→revision→second-haul works.

- [ ] **PLAY-07 — Automate the complete visible journey.**  
  **Needs:** PLAY-06, G04, G05, G02. **Deliver:** native/browser journey through shipped actions, including save/reload, rescue and both jobs, with screenshots/counters/timing. **Pass:** actual controls produce success; no substituted physics, direct success injection, hidden reduced workload, or debug-only dependency.

- [ ] **PLAY-08 — Run and respond to the human fun test.**  
  **Needs:** PLAY-07. **Deliver:** actual 8–12-person observations against the inline human targets, reasons for confusion/failure, and implemented follow-up fixes. **Pass:** targets pass or the product is explicitly revised and retested. Agents cannot synthesize participant reports; continue independent engineering if people are unavailable.

- [ ] **G06 — First two-job proof is worth expanding.**  
  **Needs:** G02, G03, G04, G05, PLAY-01, PLAY-02, PLAY-03, PLAY-04, PLAY-05, PLAY-06, PLAY-07, PLAY-08. **Pass:** real players build, recover, return, improve and voluntarily play again; durable state and visible performance evidence accompany the result.

## Phase 7 — general assemblies, crane, and structural damage

- [ ] **MECH-01 — Generalize construction beyond the starter sockets.**  
  **Needs:** G06, SIM-01, DATA-05. **Deliver:** full permitted lattice/orientations, arbitrary valid weld connections, disconnected-component handling, multi-root compile and over-limit rejection within the stated budgets. **Pass:** diverse 256-part builds compile consistently; socket fit, collision and visible geometry agree; invalid disconnected launch states have clear feedback.

- [ ] **MECH-02 — Implement the powered-crane decision spike.**  
  **Needs:** MECH-01, SIM-09. **Deliver:** hinge limits/motors/force caps and a loaded crane test: long arm, high mass ratio, grounded base, loop/limit cases, slack-to-taut line and two interacting machines. **Pass:** record stability, error, energy, frame/tick cost, input latency and implementation effort against declared tolerances; no sparse-body benchmark substitutes.

- [ ] **MECH-03 — Select and finish the machine solver route.**  
  **Needs:** MECH-02. **Deliver:** recorded GPU extension versus explicit CPU-adapter decision. Default stays WebGpuSoft when it meets stability/development goals. If CPU wins, integrate shapes, LEGO terrain, handles, events, transactions, joints, water and bounded render uploads. **Pass:** one solver owns each authoritative body; all earlier game/save tests pass under the selected route.

- [ ] **MECH-04 — Estimate structural loads and give warnings.**  
  **Needs:** MECH-03, DATA-06. **Deliver:** bounded contact/winch/module load distribution onto bonds, material-strength parameters, accumulated damage, creak/strain events and tool-driven cuts. **Pass:** controlled lever/brace fixtures fail in understandable locations; warnings correspond to actual risk; no claim of unavailable welded-joint impulses or full FEM.

- [ ] **MECH-05 — Commit fracture and meaningful fragments atomically.**  
  **Needs:** MECH-04, SIM-06. **Deliver:** graph split, reserved new bodies/shapes, mass/inertia, joint retargeting, collision-cache invalidation, render mapping and ownership under one revision. **Pass:** post-solve velocity inheritance preserves mass/momentum; impacts are not doubled; repeated split/reset/reuse leaves no stale parts/endpoints; full pools never erase important cargo.

- [ ] **MECH-06 — Add repair, cutting, and assembly recovery.**  
  **Needs:** MECH-05, PLAY-05. **Deliver:** repair/cut tools, restricted field replacement, workshop reattachment with inventory and momentum policies, damaged module behavior and rescue integration. **Pass:** damage changes function/readability; repair costs/refunds are exact; no free-motion welding exploit or unreachable essential component.

- [ ] **MECH-07 — Add compartments, breaches, and flooding.**  
  **Needs:** MECH-06, SIM-05, REND-03. **Deliver:** authored sealed/open volume contract, breach/flood state, pumps/patching, flotation/mass response and matching interior water/exclusion. **Pass:** controlled breach changes motion and visual water correctly; displaced water and interior mass are not double-counted; draining/reload/repair is consistent.

- [ ] **MECH-08 — Complete dynamic checkpoints and bounded replay.**  
  **Needs:** MECH-07, SAVE-04. **Deliver:** all Contract E states, certified tick snapshots, bounded authoritative replay checkpoints/events and semantic resume for articulated/damaged/flooded craft. **Pass:** interrupted saves with detached cargo, partial cuts, latched loads and motor targets recover; replay shows meaningful events without claiming cross-GPU bit-exact input replay.

- [ ] **G07 — General machine foundation complete.**  
  **Needs:** MECH-01, MECH-02, MECH-03, MECH-04, MECH-05, MECH-06, MECH-07, MECH-08. **Pass:** a constructed crane craft loads, breaks, repairs, saves/reloads and finishes both jobs with consistent simulation/render/ownership revisions.

## Phase 8 — final-quality slice: presentation, player, and tools

- [ ] **VIS-01 — Implement the assembly renderer.**  
  **Needs:** SIM-06, REND-04, ASSET-03. **Deliver:** current/previous root poses, local-part tables, root-first culling, part-cluster LOD, mesh/material bins, indexed indirect draws and bounded outputs. **Pass:** exercise 256-part craft and 8,192 detailed visible-part targets; moving GPU roots do not require per-part CPU pose uploads; resets/splits leave no stale instances.

- [ ] **VIS-02 — Add motion history and temporal reconstruction.**  
  **Needs:** VIS-01. **Deliver:** camera/object motion, jitter policy, unjittered cache identity, depth/material/ID/topology rejection, reactive water/foam, rebase/teleport handling and sharp reference mode. **Pass:** moving views reduce shimmer without objectionable blur/trails; splits/edits have correct history; cache refresh cost matches the declared design.

- [ ] **VIS-03 — Finish materials, LODs, and visual damage.**  
  **Needs:** VIS-02, ASSET-06, MECH-05. **Deliver:** final production meshes/materials for the slice kit including the pontoon, consistent plastic/metal/wetness response, pixel-footprint stud/bevel/seam LOD, broken interiors and damage states. **Pass:** independent visual review approves the actual playable kit; important silhouettes/socket cues survive LOD; directional lighting exposes no baked-light artifacts; color/exposure matches terrain/water and remains readable in motion.

- [ ] **VIS-04 — Add local water and impact effects.**  
  **Needs:** VIS-03, SIM-05, DATA-06. **Deliver:** pooled wakes, propeller foam, splashes, drained water, dust/impact effects and readable rope rendering. **Pass:** effects follow actual events/forces, respect depth/medium ordering, fade within bounds and do not alter authoritative cargo or exceed their allocation budget.

- [ ] **VIS-05 — Produce the final cove and harbor art.**  
  **Needs:** ASSET-07, VIS-04, PLAY-06. **Deliver:** coherent foreground/mid-distance/horizon; workshop, wreck, crane, navigation landmarks, shore treatment and a final weather state. **Pass:** independent review of playable captures approves silhouette, lighting, material consistency, water integration and navigation; no required placeholder is hidden by camera framing.

- [ ] **UX-01 — Finish workshop and blueprint management.**  
  **Needs:** PLAY-02, MECH-01, G04. **Deliver:** symmetry, multi-select, move/replace, paint, named build library, duplicate/import/export, optional COM/flotation/stress views and clear invalid-placement reasons. **Pass:** controller and keyboard/mouse can construct, name, test, undo, save, reload and launch without inventory drift; model diagnosis uses understandable labels.

- [ ] **UX-02 — Complete application screens and input lifecycle.**  
  **Needs:** PLAY-03, UX-01. **Deliver:** title/new/load, job board, map, inventory, workshop, on-foot/driving, pause, rescue, results and settings; text entry, focus/modal/back rules, rebinding, sensitivity/inversion/deadzones and hold/toggle alternatives. **Pass:** hot input switching, controller reconnect, focus loss and resize cannot trap input or trigger unintended actions.

- [ ] **UX-03 — Define safe test-launch and pause behavior.**  
  **Needs:** UX-02, SAVE-04. **Deliver:** workshop test mode uses a temporary copy and clearly discards test damage/rewards on exit; expedition launch uses authoritative resources/state. Solo pause freezes simulation; co-op UI later honors the shared-world rule. **Pass:** no test-mode reward/inventory leakage; restart/quit behavior is clear and recoverable.

- [ ] **UX-04 — Add accessible onboarding and localization structure.**  
  **Needs:** UX-02, PLAY-06. **Deliver:** action-based tutorials, short repeatable help, scalable text/UI, icon+color cues, captions, reduced motion, remappable controls, localization keys and expansion space. **Pass:** full controller-only journey and dyslexia/accessibility sessions reveal no mandatory unreadable or unreachable step; instructions never require long paragraphs to proceed.

- [ ] **ACT-01 — Build the robot asset and actual animation runtime.**  
  **Needs:** ASSET-03, ASSET-06, PLAY-03. **Deliver:** rig or rigid-node animation, sampling/blending/hierarchy evaluation, locomotion and tool/helm interaction states. Implement skinning explicitly if used. **Pass:** exported animations run in native/WASM, with correct normals/bounds/anchors; declared but unused VMESH skin fields are not treated as implementation.

- [ ] **ACT-02 — Implement dock locomotion and camera collision.**  
  **Needs:** ACT-01, SIM-03, UX-02. **Deliver:** grounded movement, jumps/steps, robust spawn, third-person camera collision, orbit/chase transitions, load visibility and reduced-motion settings. **Pass:** cliffs, studs, slopes and nearby machinery do not cause camera clipping or character penetration; transitioning controls remains predictable.

- [ ] **ACT-03 — Support moving decks, boarding, and water recovery.**  
  **Needs:** ACT-02, MECH-03, SIM-05. **Deliver:** moving-platform contact/frame handling, inherited velocity, boarding/helm occupancy, exit/fall states, simple swimming and rescue. **Pass:** walk/jump/fall on rolling/turning boats and moving links without artificial drag or velocity loss; a player cannot be trapped by a sunken craft. Multiplayer reconciliation follows in NET tasks.

- [ ] **SND-01 — Implement event-driven audio and optional haptics.**  
  **Needs:** DATA-06, UX-02. **Deliver:** native/browser audio backend, bounded voices, priorities, spatialization, buses, persistent volume settings, browser unlock and optional intensity-controlled haptics. **Pass:** output-device changes and focus/suspend recover; overload is bounded; mute/captions/reduced-motion preserve critical information.

- [ ] **SND-02 — Author the slice soundscape.**  
  **Needs:** SND-01, MECH-06, VIS-04. **Deliver:** engine-load/propeller, winch strain, plastic stress, material impacts, splashes, latches, cutting/repair, UI/rewards, ambient coast and music states. **Pass:** recorded actual playback matches events and remains intelligible under load; graph execution alone does not prove sound quality or output.

- [ ] **G08 — One final-quality expedition is complete.**  
  **Needs:** G07, VIS-01, VIS-02, VIS-03, VIS-04, VIS-05, UX-01, UX-02, UX-03, UX-04, ACT-01, ACT-02, ACT-03, SND-01, SND-02. **Pass:** independent visual/audio review and renewed human sessions approve both jobs through shipped controls; moving-deck behavior, saves, performance, accessibility and recovery work together.

## Phase 9 — scalable authoring and bounded world loading

- [ ] **WORLD-01 — Add part and mission authoring tools.**  
  **Needs:** G01, ASSET-07, PLAY-06. **Deliver:** part test scene and site/mission authoring for sockets, modules, cargo IDs, objective predicates, extraction, prerequisites, rewards, spawns and recovery. **Pass:** a designer adds one new valid pontoon and one complete recovery job without editing C++ or WGSL; errors identify the offending asset/property.

- [ ] **WORLD-02 — Integrate content identity and cooking.**  
  **Needs:** WORLD-01. **Deliver:** stable content-ID registry, dependency graph, deprecation/migration rules and runtime bundles. Include authoritative schemas, shapes, relevant shaders/code and physics parameters in compatible identity for the selected demo/campaign profile. **Pass:** mismatched authoritative content is rejected; native/browser demo identities agree while full-campaign admission rejects a demo-only client; unrelated installed bundles do not cause false incompatibility; moved/renamed assets preserve saves through explicit migration; both build systems package the same manifests.

- [ ] **WORLD-03 — Split experience packages and load asynchronously.**  
  **Needs:** WORLD-02, SAVE-04. **Deliver:** load only selected-mode essentials, background bundle loading/decoding, bounded staging/upload queues, deduplicated resources and clear readiness states. **Pass:** unrelated prototypes are not preloaded by salvage; interactive areas require ready collision/gameplay data; missing/corrupt bundles cannot corrupt the canonical save.

- [ ] **WORLD-04 — Implement region and assembly residency.**  
  **Needs:** WORLD-03, MECH-08. **Deliver:** sector-local state, camera-relative rendering, checkpoint/unload rules, dependency-group pinning and the whole-party region-transition state machine. **Pass:** repeated travel/unload/reload retains changes and has bounded memory; occupied craft/crew/cargo transfer together after leases and pending edits settle; interruption recovers the prior or fully committed destination region; no rope spans regions and valuable abandoned cargo follows its recovery rule.

- [ ] **WORLD-05 — Add safe cancellation and GPU resource retirement.**  
  **Needs:** WORLD-04, SIM-04. **Deliver:** cancellation of obsolete loads/cooks, revision/content checks, completed-fence retirement, device-loss reload and bounded pending resources. **Pass:** teleport/reset/cancel/device recovery under ongoing uploads leaves no leaks, use-after-free, stale assets or false-completed work.

- [ ] **WORLD-06 — Add compressed-texture support and quality presets.**  
  **Needs:** WORLD-03, VIS-03. **Deliver:** versioned cooker/runtime support for KTX2/Basis or an equivalently documented supported format, target-format selection, shared-material deduplication and explicit presets. **Pass:** decoded appearance matches reference and memory/loading improve; unsupported formats fall back deliberately. Existing VMESH RGBA8 data is not mislabeled as compression.

- [ ] **WORLD-07 — Resolve terrain residency from actual region sizes.**  
  **Needs:** WORLD-04, WORLD-06, VIS-05. **Deliver:** measured per-region terrain/collision/shadow memory; retain resident compact regions if budgets pass, otherwise implement real chunk/mip streaming with conservative traversal and collision readiness. **Pass:** selected design supports all four planned region sizes, teleports and boundaries without silent geometry changes; material-atlas streaming is not counted as terrain streaming.

- [ ] **WORLD-08 — Measure content-production throughput.**  
  **Needs:** G08, WORLD-01, WORLD-02. **Deliver:** timed source→export→cook→in-game→review cycles for a final part and site; a staffing/time forecast with real measured rates and remaining tooling blockers. **Pass:** another content creator reproduces the process; the campaign estimate includes integration/review and rework, not only mesh generation.

- [ ] **G09 — Production content can be made and loaded safely.**  
  **Needs:** WORLD-01, WORLD-02, WORLD-03, WORLD-04, WORLD-05, WORLD-06, WORLD-07, WORLD-08. **Pass:** second-region test content loads, plays, unloads and restores within documented memory/time budgets, and a designer can deliver new content without source edits.

## Phase 10 — real co-op, first two players and then four

- [ ] **NET-01 — Generalize the host authority and protocol.**  
  **Needs:** MECH-08, DATA-05, WORLD-02. **Deliver:** host-owned expedition state, participant IDs/leases, canonical command ordering, idempotent receipts, compatible content checks and bounded protocol schema. **Pass:** two in-process participants cannot duplicate edits/rewards; every canonical field has one authority; legacy WRECKWATER's 11-body scope is not inherited as claimed capacity.

- [ ] **NET-02 — Add real secure native and browser transports.**  
  **Needs:** NET-01, BOOT-04. **Deliver:** encrypted transport, admission/authentication, host/discovery/join/leave flow and explicit deployment adapter configuration. Native hosts support matching native/browser demo participants; full-campaign sessions require native campaign clients. Reuse transport-independent session code. **Pass:** actual processes join over the selected transport; browser code is wired to the demo expedition and profile compatibility errors are readable. Browser hosting is not required. Credentials and access are provisioned explicitly, never fabricated.

- [ ] **NET-03 — Replicate roots, topology, and dependency groups.**  
  **Needs:** NET-02, VIS-01, WORLD-04. **Deliver:** bounded root/joint snapshots, reliable topology/economic events, epoch/revision handling, unknown-revision queue/baseline requests and dependency-aware interest. **Pass:** out-of-order split/motion delivery never produces invalid root mappings; rope/cargo endpoints cannot disappear independently; bytes and queue peaks are measured.

- [ ] **NET-04 — Add prediction, correction, and crew interactions.**  
  **Needs:** NET-03, ACT-03. **Deliver:** responsive input, occupied/towed-machine-aware prediction policy, interpolation/correction, moving-platform reconciliation, helm/repair/winch interaction ownership and edit leases. **Pass:** two players walk/operate one moving craft without fighting authority; corrections are bounded and visible tests show acceptable control feel.

- [ ] **NET-05 — Implement reconnect and host save ownership.**  
  **Needs:** NET-04, SAVE-02, MECH-08. **Deliver:** disconnect-neutral controls, rejoin baseline, host-owned progression, guest session state, canceled leases, durable receipts/processed markers and explicit host-departure flow. **Pass:** disconnect during latch/bank/edit and retries after receipt eviction/checkpoint/rejoin cannot duplicate or lose cargo/inventory; retired tokens stay invalid; guests cannot transfer resources to another world; host departure follows documented recovery behavior.

- [ ] **NET-06 — Validate two real rendered clients under adversity.**  
  **Needs:** NET-05, G08. **Deliver:** two-client complete haul, boarding/cutting/latching/return/save/reconnect, and transport adversity tests at ≥100 ms RTT, jitter, 2% loss, duplication and reordering. **Pass:** final canonical ownership agrees; actual packet/transport behavior and deterministic frame-adversity tests are distinguished; local latency/correction quality is reported.

- [ ] **G10 — Two-player co-op is proven.**  
  **Needs:** NET-01, NET-02, NET-03, NET-04, NET-05, NET-06. **Pass:** two real clients complete and resume an expedition without duplicates, broken topology, or unusable moving-deck control.

- [ ] **NET-07 — Expand to four-player launch workload.**  
  **Needs:** G10, G09. **Deliver:** four-player roster/UI, multiple occupied craft, simultaneous tools/edits, whole-party travel, full launch-capacity budget and interest/host-load tuning. **Pass:** four actual clients complete shared/separate jobs inside one active region and travel together; mixed native/browser demo clients obey the same profile limits; combined roots/joints/cargo stay within world limits; one client cannot starve session work.

- [ ] **NET-08 — Finish social coordination and hosted operation.**  
  **Needs:** NET-07. **Deliver:** room/invite or connection workflow, contextual pings, player/build names, host permissions, session errors and operating instructions; sufficient service monitoring for the chosen hosting route. Voice/chat services are outside mandatory scope unless added explicitly. **Pass:** unfamiliar players join, coordinate, reconnect and leave without developer help or private debug arguments.

- [ ] **G11 — Launch multiplayer count is proven.**  
  **Needs:** NET-07, NET-08. **Pass:** four real rendered clients pass the complete expedition and adversity/reconnect suite at declared client/host budgets. A two-client gate cannot substitute.

## Phase 11 — produce the complete game

These are working region names, not finished branding. Each region needs a distinct recovery problem, landmarks, workshop access, a safe recovery route, optional discoveries, and readable progression. Region transitions may use authored passages/loading; a seamless planet is not required.

| Region | Required gameplay identity | Main progression role |
|---|---|---|
| Harbor Reach | Sheltered water, nearby wrecks, introductory towing and loaded returns | Starter craft, generator/lift restoration, first sidegrades |
| Cliffworks | Elevated platforms, heavy suspended loads, constrained access | Powered cranes, cutting, bracing, load-path diagnosis |
| Marsh Delta | Shallows, channels and land approaches that make pure boats inconvenient | Amphibious mobility, traction, route and weight tradeoffs |
| Stormbreak | Exposed water, flooding risk and multi-stage recovery | Combined mastery and final harbor restoration |

Campaign progression must unlock useful capabilities and new recovery opportunities. Difficulty comes from interacting systems with multiple valid solutions. A job may recommend a capacity/tool; it must not require one exact hidden build. Stormbreak needs forecast/readability and safe exits, not randomly unavoidable losses.

- [ ] **CONTENT-01 — Establish the staffed production schedule.**  
  **Needs:** G08, WORLD-08, BOOT-04. **Deliver:** remaining work breakdown, named ownership/capacity for engine/physics, gameplay/network, technical art/tools, environment/character art, design/UI, sound and QA; critical path, integration cadence, operating costs and risk reserve. **Pass:** dates use measured content throughput and actual staffing. As an initial hypothesis only, baseline/contracts may take 4–8 weeks, the playable proof 8–16, the final-quality slice 12–24, and production tooling/co-op 8–16, with overlap only where dependencies permit. Re-estimate full production after the slice; do not promise a AAA campaign on those milestone estimates alone. Budget using staff-months × actual fully loaded rates + contractors/services/hardware + a stated contingency (initial planning range 25–35%). AI generation time is not a substitute for integration, animation, design review, or QA.

- [ ] **CONTENT-02 — Author the campaign and economy graph.**  
  **Needs:** G09, PLAY-08, CONTENT-01. **Deliver:** all required/optional jobs, prerequisites, material sources/sinks, capability unlocks, harbor restoration stages, finale, recoverability rules and a route through all four regions. **Pass:** automated graph checks and designer walkthroughs find no unreachable mandatory objective, consumed unique-key dead end, repeat-reward exploit or forced grind after rescue; first-time players understand why they want the next machine.

- [ ] **CONTENT-03 — Implement amphibious running gear.**  
  **Needs:** G07, MECH-03, WORLD-01, G11. **Deliver:** powered wheels, steering, suspension/contact model, brakes, drive/traction tuning, relevant editor diagnostics, and water/shore transition handling. **Pass:** an actual player-built hauler drives on slopes and soft-looking authored terrain, enters/exits water, transports cargo and saves/reloads. Wheel/suspension forces derive from contact/compression and buoyancy from submerged volume; their combined reaction approaches weight at equilibrium, rather than assigning full weight to each system. Test continuous wheel unloading with immersion, loss of ground contact, low/high speed, uneven terrain, varied COM, articulation and four-player replication. Traction parameters, slope limits and supported surfaces are data, not hidden per-mission hacks.

- [ ] **CONTENT-04 — Complete the functional part library.**  
  **Needs:** CONTENT-02, CONTENT-03, G02, MECH-06, WORLD-02. **Deliver:** 50–70 distinct functional definitions across structure/flotation, power/control, towing/latching, crane/tool, repair and amphibious families; production meshes/materials/LODs, sockets, colliders, mass/inertia/buoyancy, costs, damage variants, names/icons and localization keys. **Pass:** each part runs through authoring/import validation and a functional demonstration; representative combinations, awkward socket rotations and maximum craft sizes are tested. The catalog supports meaningfully different craft; cosmetic colors and trivial size variants do not satisfy the count.

- [ ] **CONTENT-05 — Finish Harbor Reach and Cliffworks.**  
  **Needs:** CONTENT-02, CONTENT-04, G09, G11. **Deliver:** final landmarks/sites/wreck assemblies, jobs, cargo, environmental lighting/sound and workshop access for the first two regions. **Pass:** solo and co-op crews complete each required branch using at least two viable engineering approaches where appropriate; travel, editing, streaming, cutting, cargo recovery and harbor progress survive save/rejoin.

- [ ] **CONTENT-06 — Finish Marsh Delta and Stormbreak.**  
  **Needs:** CONTENT-05, CONTENT-03. **Deliver:** final amphibious sites, exposed-water recovery chains, clear wave/weather cues, safe harbors, late capability challenges and finale. **Pass:** cargo remains recoverable, all required tools have been introduced, topology/memory budgets hold, and the final mission combines learned systems without an unexplained new control or exact-build requirement.

- [ ] **CONTENT-07 — Ship separate creative mode and blueprint exchange.**  
  **Needs:** UX-01, MECH-08, CONTENT-04, NET-05. **Deliver:** separate creative-world saves, unlocked parts, free spawning, reset/undo, blueprint library, preview and versioned local file import/export. **Pass:** campaign ownership/resources cannot be imported from creative; blueprints reconstruct through current validation/migration and consume resources when physically spawned in campaign. Local file sharing is mandatory. Public galleries, account inventory and moderation services are deferred unless the product contract is extended.

- [ ] **CONTENT-08 — Complete production art, narrative, and sound.**  
  **Needs:** CONTENT-06, CONTENT-07, ACT-03, SND-02, ASSET-07. **Deliver:** coherent final assets for all four regions, character animations, brief job text, world signage, completion feedback, audio mix/music, VFX and credits/provenance. **Pass:** the shipped campaign contains no unintentionally visible debug art, reference-image labels, missing clips, silent required cues or inconsistent material scale; visual/audio review checks complete actual playthroughs, not only promotional renders.

- [ ] **CONTENT-09 — Tune the whole campaign with human playthroughs.**  
  **Needs:** CONTENT-08, G11. **Deliver:** solo and co-op first-time playthrough observations, build diversity, completion time, job abandonment, recovery pain, economy and optional/post-finale play. **Pass:** representative first-time playthroughs support the 12–18 hour target and show understandable, useful design iteration; fix padding, dead time, dominant builds, unrecoverable cargo and confusing progression. Repeat the affected playthroughs after material changes; do not infer duration by multiplying mission estimates.

- [ ] **G12 — Full game content complete.**  
  **Needs:** G09, G11, CONTENT-01, CONTENT-02, CONTENT-03, CONTENT-04, CONTENT-05, CONTENT-06, CONTENT-07, CONTENT-08, CONTENT-09. **Pass:** all four regions, final campaign, creative mode, required machine families and functional catalog work as one integrated solo/four-player game. The polished cove alone cannot satisfy this gate.

## Phase 12 — release-candidate reliability and quality

- [ ] **QA-01 — Create a release test matrix and reproducible workloads.**  
  **Needs:** G00, G08, G12. **Deliver:** reproducible installable Windows/Linux and browser-demo candidate packages with immutable code/content/artifact manifests; named hardware/OS/browser/preset matrix, maximum supported scenes, fresh/aged saves, varied mass ratios, moving crews, dense contacts, ropes/cranes, fracture bursts, full cargo and region boundaries. **Pass:** QA installs the actual candidate without development tools; tests identify exact package hashes, capacity settings and expected bounded workloads; 30/60/144 Hz presentation does not change authoritative 60 Hz behavior. Packaging changes invalidate affected candidate evidence.

- [ ] **QA-02 — Meet measured performance and memory budgets.**  
  **Needs:** QA-01. **Deliver:** visible native Windows/Linux and browser-demo CPU/GPU/presentation/latency captures, steady state and cold/warm loading, worst supported topology changes, four-client host load, and memory by category including peak staging and pending GPU retirement. **Pass:** the declared targets in this file and BOOT-04 matrix pass. Report p50/p95/p99, hitches, frame misses, queue age and measurement limitations; maintain at least 20% measured WASM heap headroom at peak on the supported demo route, or revise the heap/package contract before acceptance. A software renderer, average FPS or cached static camera alone is insufficient.

- [ ] **QA-03 — Exercise persistence faults and shipped migrations.**  
  **Needs:** QA-01, MECH-08, NET-05, CONTENT-07. **Deliver:** kill/crash at journal/checkpoint boundaries, truncated/corrupt saves, quota/full disk, unavailable storage, stale/repeated receipts, older supported schemas, changed content IDs, missing assets and interrupted region transitions. **Pass:** last durable ownership and rewards recover exactly once; corrupt new data cannot destroy the last valid save; the user can identify/recover an unsaved state. Browser demo storage failures are covered even though the full campaign is native at launch.

- [ ] **QA-04 — Harden untrusted content and network boundaries.**  
  **Needs:** QA-01, NET-08, CONTENT-07. **Deliver:** bounded parser/command/property-based and fuzz tests for blueprint files, saves, protocol messages and cooked asset headers; sizes/counts, NaN/infinity, bad IDs/revisions, decompression limits and path traversal rejection. **Pass:** malformed data cannot allocate unbounded work, reference arbitrary files, bypass resource/host authority, or crash a session; accepted content is validated before simulation and GPU upload. This does not require a public mod marketplace.

- [ ] **QA-05 — Run long-session physics and co-op reliability tests.**  
  **Needs:** QA-01, QA-03, QA-04. **Deliver:** at least a four-hour four-client representative soak plus targeted overnight automated topology/save/load stress; disconnect/jitter/loss/reordering, repeated boarding, fracture, banking, repair and cross-region travel. **Pass:** no unbounded growth, invalid body handles, runaway energy, deadlocks, duplicated cargo or permanent desynchronization; document failure seeds and reduced reproductions. Automated soak does not replace human control/comfort review.

- [ ] **QA-06 — Validate lifecycle and existing-mode regressions.**  
  **Needs:** QA-01, WORLD-05, UX-02. **Deliver:** install/first launch, shader compilation, reload/reset, resize/DPI/fullscreen, controller hotplug, focus/pointer-lock loss, suspend/resume, browser tab throttling, GPU/device loss and unavailable audio tests. Preserve LEGO, smooth terrain, RIDGEBREAK and WRECKWATER smoke coverage. **Pass:** state remains coherent, queues stay bounded, returning from suspension does not simulate an unbounded catch-up burst, and errors offer a usable retry/exit/save recovery path. Record platform cases that need real hardware.

- [ ] **QA-07 — Complete accessibility and language review.**  
  **Needs:** QA-01, UX-04, CONTENT-08. **Deliver:** real player evaluation of remapping, controller-only flow, text/UI scaling, contrast, non-color cues, motion/camera options, audio alternatives, motor-demand settings and difficulty/recovery assists. Ship English and localization-ready data; add other languages only with actual translation/review ownership in BOOT-04. **Pass:** all essential actions/objectives work through supported inputs and assist settings; text expansion and pseudo-localization do not clip or break layouts; readable tutorials do not depend on spoken developer help.

- [ ] **QA-08 — Close defects and repeat human acceptance.**  
  **Needs:** QA-02, QA-03, QA-04, QA-05, QA-06, QA-07, CONTENT-09. **Deliver:** prioritized defect ledger with reproductions, resolutions and release disposition; renewed first-session and whole-campaign human checks on the candidate build. **Pass:** no known critical crash, save loss, progression dead end or duplicate-reward defect; other accepted limitations have an accountable release decision and user-facing documentation where needed. Human gates use actual participants and the exact candidate, with retests scoped to changes.

- [ ] **G13 — Release candidate verified.**  
  **Needs:** G00, G01, G12, QA-01, QA-02, QA-03, QA-04, QA-05, QA-06, QA-07, QA-08. **Pass:** product scope, performance, durability, co-op, accessibility, visual/audio quality and human gates have traceable evidence. External hardware/testing gaps remain unchecked; do not replace them with confident prose.

## Phase 13 — package, release, and support

- [ ] **REL-01 — Produce versioned distributable packages.**  
  **Needs:** G13. **Deliver:** promote the exact tested QA-01 native Windows/Linux and bounded browser-demo candidate artifacts, with code/content/protocol/save versions, dependency/license notices, symbols and source-to-artifact manifest. **Pass:** package hashes match verified candidates; fresh machines complete the intended journey without development tools; native solo works offline; demo loading/admission obeys its catalog and session profile. A rebuilt/re-signed package returns to the affected candidate checks before promotion. Signing/distribution credentials must be obtained through the actual release process.

- [ ] **REL-02 — Test updates, rollback, and support diagnostics.**  
  **Needs:** REL-01. **Deliver:** supported update/migration path, pre-migration backup, rollback compatibility policy, build-identifiable crash/error reports, user-controlled diagnostic export and recovery/support runbook. **Pass:** update failure preserves the last durable save; an older binary does not silently overwrite a newer incompatible save; a reported issue can be traced to code/content/settings without logging secrets or unnecessary personal data.

- [ ] **REL-03 — Prepare release presentation and operating readiness.**  
  **Needs:** REL-01, REL-02, NET-08, CONTENT-08. **Deliver:** final name/branding, actual in-game screenshots/trailer captures, supported-platform/features description, controls/help, known issues, co-op hosting/join instructions, service ownership and incident/recovery procedures. Update README with current routes and accurate completion evidence while retaining legacy prototype instructions. **Pass:** advertised features match the candidate; generated concepts are not presented as real gameplay; another person can launch, host/join, recover a save and diagnose a failed connection from shipped instructions.

- [ ] **REL-04 — Complete release review and authorized distribution.**  
  **Needs:** REL-01, REL-02, REL-03. **Deliver:** accountable release sign-off, immutable candidate hashes, distribution configuration, tested staged rollout/rollback, post-release issue intake and owners. **Pass:** authorized native packages and browser demo are available through the selected distribution route and installation/join smoke tests pass there. Do all preparation before requesting any necessary final publishing/account approval; this plan itself does not create store accounts, authorize purchases or supply access.

- [ ] **G14 — Launch complete.**  
  **Needs:** G13, REL-01, REL-02, REL-03, REL-04. **Pass:** released artifacts match verified hashes, required launch features are accessible, support/rollback ownership is active, and the execution ledger links final evidence. Writing this plan or finishing engine infrastructure is not completion of the game.

## Bootstrap commands and verification routing

Run from the repository root. These are existing entry points checked during planning; revalidate them at BOOT-02. They are instructions, not evidence that a new build/test run passed. Use a bounded parallel build size appropriate to the machine.

```bash
# Enter the project toolchain first.
nix-shell

# Only when vendored dependencies are missing or require their pinned refresh.
./scripts/fetch_deps.sh

# Native engine and existing asset importer.
bazel build -c opt //:voxy_native //tools:gltf_vmesh_tool

# Focused starting coverage; this is not the complete suite.
bazel test //tests:config //tests:lego_surface //tests:lego_playground //tests:gltf_vmesh_tool_test

# Repository-wide suite used by the existing commit hook.
bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test

# Browser build and development server; the server is long-running.
bazel build --config=wasm //:voxy_wasm
bazel run --config=wasm //tools:serve_wasm
```

Run the alternative native build separately, after checking its options in the current `CMakeLists.txt`:

```bash
cmake -S . -B build-salvage-native -DVOXY_BUILD_NATIVE=ON -DVOXY_BUILD_TESTS=ON -DVOXY_BUILD_TOOLS=ON
cmake --build build-salvage-native -j 4
ctest --test-dir build-salvage-native --output-on-failure

# Required whenever shared LEGO surface constants/formulas change.
python3 scripts/sync_lego_surface.py --check
```

WASM builds require the repository's Emscripten setup. At this baseline `.github/workflows/pages.yml` pins Emscripten 6.0.1; confirm the active pin and dependency/toolchain paths rather than assuming the shell supplies them. Inspect `.bazelrc`, `build/`, `scripts/` and CI before changing the toolchain. CMake WASM configuration must use the Emscripten toolchain and explicitly disable native targets as appropriate; do not treat a host CMake build as WASM validation.

For a new asset, the current importer takes two positional paths:

```bash
# Replace both absolute paths with actual reviewed source/output paths.
bazel run //tools:gltf_vmesh_tool -- /absolute/path/part.glb /absolute/path/part.vmesh
```

ASSET-01 must create the Blender generator and document its actual CLI before others invoke it. The planned pattern is `blender --background --factory-startup --python-exit-code 1 --python <generator.py> -- <asset arguments>`. No such general generator is assumed to exist today. Start from a clean scene, write to a distinct output path, inspect the exported result, and preserve hand-authored source files. A successful Blender process does not prove importer compatibility or good game collision.

Verification routing:

| Changed area | Required checks beyond compilation |
|---|---|
| Shared schemas/commands | Canonical encoding, malformed/repeated requests, atomic rollback, save/protocol compatibility |
| Assembly compiler/physics | Analytic mass/inertia, all shape consumers, contacts/CCD/queries, momentum, fixed-tick water, capacity rejection |
| Gameplay/economy | Actual controls through both jobs, resource/ownership invariants, rescue, save/reload, co-op races |
| Rendering/shaders | Shared-source check, validation layer, visible native/browser images, underwater ordering, motion/history, measured full-frame cost |
| Assets/cooking | Source manifest, units/axes/materials, metadata correspondence, engine preview, collision/socket/LOD behavior |
| Streaming/lifecycle | Cancel/unload/device loss, bounded memory, collision readiness, save identity and retirement fences |
| Networking | Real rendered clients, adversarial ordering/loss, revision recovery, authority/economy, moving crew and host load |

Use focused tests while implementing, then the relevant gate's full integrated route. Test labels for new salvage modules must be added to both build systems; the existing suite cannot cover code that is never compiled or registered. Run required repository checks before committing. A documentation-only edit does not justify claiming new runtime verification.

## Repository source map

These files are starting points; inspect their current symbols before editing. This map does not require an agent to read the earlier blueprint or conversation.

| Responsibility | Existing source paths |
|---|---|
| Instructions/build entry points | `AGENTS.md`, `README.md`, `BUILD`, `CMakeLists.txt`, `.bazelrc`, `shell.nix`, `flake.nix` |
| Shared mode/lifecycle/input integration | `src/app/application.hpp`, `src/app/application.cpp`, `web/index.html`, `src/engine/platform/wasm/entry.cpp` |
| Playground state and legacy limits | `src/game/lego_playground.hpp` |
| Brick-like terrain surface and layout | `src/terrain/lego_surface.hpp`, `src/terrain/lego_layout_cache.hpp`, `shaders/lego_surface.wgsl`, `scripts/sync_lego_surface.py` |
| Physics API and backend boundary | `src/physics/physics_types.hpp`, `src/physics/physics_backend.hpp`, `src/physics/gpu/gpu_physics_backend.cpp` |
| Collision scale and primitive drawing | `shaders/physics_broad_phase.wgsl`, `shaders/physics_primitives_compact.wgsl`, `src/render/primitive_path.cpp` |
| Structural connectivity/damage | `src/gameplay/structural_assembly.hpp`, `src/gameplay/structural_assembly.cpp` |
| Water fields and composition | `src/render/water_simulation.cpp`, `shaders/water_clipmap.wgsl`, `src/render/raycast_path.cpp`, `src/render/blit_path.cpp`, `shaders/ray_blit.wgsl` |
| Mesh importer/runtime format | `tools/gltf_vmesh_tool.cpp`, `src/moto/vmesh.hpp`, `src/render/mesh_path.hpp`, `shaders/mesh_path.wgsl` |
| Existing authority and replay references | `src/server/wreckwater_authority_runtime.hpp`, `src/server/wreckwater_authority_runtime.cpp`, `src/game/wreckwater_replay.hpp` |
| Build/test/packaging integration | `tests/BUILD`, `tools/BUILD`, `.github/workflows/pages.yml`, `scripts/fetch_deps.sh` |
| Existing browser profiling | `scripts/benchmark_browser.mjs`, `scripts/benchmark_wasm_render.mjs`, `docs/performance-profiling.md` |

Search related headers/tests/callers with `rg` before extending a type. Changing a shape or body layout requires auditing every CPU/WGSL producer/consumer, buffer stride and build inclusion. Renaming the old LEGO or WRECKWATER symbols wholesale is not a prerequisite.

## Decision ledger

The entries below are the starting implementation defaults. A task may refine unspecified details within its scope. Changing an explicit contract requires updating this table, affected requirements/tests, and downstream owners. Do not let a decision live only in a chat message.

| Decision | Initial default | Owner task / allowed resolution point |
|---|---|---|
| D01 Product/platform | Solo + creative + 1–4 co-op; Windows/Linux; desktop WebGPU demo; four regions; 50–70 functional parts | BOOT-04; later scope change must name added/removed gates |
| D02 World representation | Heightfield terrain; explicit editable wrecks; clustered machines | DATA-03, SIM-01; no implied voxel excavation |
| D03 Units/coordinates | Contract A; isotropic .02 m integer placements; 1 stud = 1 m, .32 m plate pitch, 24 proper rotations | DATA-01; migration required for a later change |
| D04 Mechanics ownership | GPU default; one authoritative backend per interacting island/world; explicit measured CPU alternative | MECH-03, before general fracture/network production |
| D05 Water authority | Versioned fixed-tick gameplay field consistent with rendering; CPU query approximation is not presumed exact | SIM-05; record sampling/error policy and acceptance fixtures |
| D06 Persistence | Host-owned world; certified checkpoints + durable receipts/journal; semantic replay | SAVE-01, MECH-08, NET-05 |
| D07 Cargo latching | Independent while towed; incorporated once into rigid cluster while latched | SIM-08; update compiler/save/network contract together if changed |
| D08 Building/recovery | Workshop major edits; limited field tools; protected starter/blueprints/unique progression cargo | PLAY-02, PLAY-05, UX-01 |
| D09 Art pipeline | Reproducible Blender source/scripts + GLB/metadata + cooker; MCP optional; image generation assists authored materials | ASSET-01 through ASSET-07 |
| D10 Transport/hosting | Host authority with real encrypted native/browser adapter; no lockstep or host migration | NET-02; record selected protocols, compatibility, topology and provisioning |
| D11 Memory/terrain | Per-mode packages, bounded queues; measure regional resident terrain before choosing chunk streaming | WORLD-03 through WORLD-07 |
| D12 Launch quality | Stated hardware/frame/capacity/human gates; English plus localization-ready data | BOOT-04, QA-01; threshold changes require rationale and new measurements |
| D13 Schedule/resources | Preliminary milestone ranges only; production forecast based on measured throughput and real staff | CONTENT-01; no fabricated release date or funding |
| D14 Region travel | One active region; separate local jobs; whole-party checkpointed harbor transitions | WORLD-04, NET-07 |
| D15 Demo compatibility | Isolated two-job/14-definition demo; native host with native/browser guests; full campaign rejects demo-only peers | BOOT-04, WORLD-02, NET-02, QA-01 |
| D16 Early visible quality — 2026-09-07 | Owner reports the BOOT-05 cove looks substantially worse. Its primitive fixture is not an accepted visual target. Add LOOK-01 to G02 and prioritize it during asset work, before expanding content. Keep original route appearance as a regression reference. | Root; evidence: `docs/validation/salvage/BOOT-05/native-final-salvage/`, optional concept and inline art direction. No data migration or removal of final quality gates. Independent LOOK-01 review remains required. |

Add revisions here with date, decision ID, reason, evidence, affected tasks, migration consequences and responsible reviewer. “Easier to implement” alone is not evidence that a removed game requirement is unnecessary.

## Execution ledger — keep this current

Goal started on 2026-09-07: implement all tasks through G14, mark verified items complete, and commit after every successfully verified gate. No gate is complete at goal creation. The existing engine and previously generated concept image are reference foundations only.

Copy one row per claimed task. Keep handoffs factual and self-contained. Reviewers must be able to find source changes and evidence without a conversation link.

| Task ID | State / owner | Branch or revision; owned paths | Deliverable and evidence location | Review / blockers / next action |
|---|---|---|---|---|
| BOOT-01 | Complete / lego_gameplay; reviewed by root | Baseline `7563f61`; `docs/validation/salvage/BOOT-01/` | [Source inventory](docs/validation/salvage/BOOT-01/report.md) | Root independently checked HEAD/status, instructions, routes, build declarations, toolchain caches and hook state; no runtime claim |
| BOOT-02 | Complete / root; render_architecture packaging; lego_gameplay staging | `codex/salvage-implementation`; shared native/WASM builds and package fixes | [Build integration](docs/validation/salvage/BOOT-02/report.md) | Fresh Bazel/CMake native and WASM pass; 102 focused Bazel cases pass, 77 latest CMake CPU cases pass; source-matching assets and six negative package controls; failures retained; full hook suite remains a gate check |
| BOOT-03 | Complete / root; independent evidence review lego_gameplay | `scripts/` capture helpers and task evidence; source instrumentation only after review | [Measured baseline](docs/validation/salvage/BOOT-03/report.md) | Visible route and separate native/browser memory captured; presentation target unmet; sparse GPU tails, partial memory, unavailable staging/retirement and historical provenance limits explicit; later QA gates remain unchecked |
| BOOT-05 | Complete / render_architecture; root integration and source/evidence review | `salvage.cfg`, shared app/config/entrypoints, `src/game/expedition/`, preview UI and tests; final CMake native `c4dc00f1…`, WASM `d4b6575d…` | [Implementation and runtime evidence](docs/validation/salvage/BOOT-05/implementation.md) | Real browser Reset/R/Leave/re-entry, empty-world GPU retirement, final native cove and legacy launches pass. Two product reset bugs and one harness failure preserved/fixed. WRECKWATER graphical launch limitations explicit. Technical placeholder only; owner rejects current visual quality; LOOK-01 remains required |
| G00 | Accepted / root; independent final review lego_gameplay | `codex/salvage-implementation`; checkpoint subject `G00: establish expedition baseline and verified preview lifecycle` | [Handoff and checks](docs/validation/salvage/G00/report.md), [accepted independent review](docs/validation/salvage/G00/review.md) | All BOOT tasks and mandatory suite passed: 1,495 C++ passes, three skips, four pre-existing disabled cases; importer cached pass with one skip. Enabled-hook checkpoint is the next operation; record resulting hash afterward. Unfinished DATA-03/ASSET-02/LOOK-01 excluded. Visual quality remains unapproved |
| BOOT-04 | Complete / root; reviewed by lego_gameplay | `docs/validation/salvage/BOOT-04/` | [Scope/ownership/hardware matrix](docs/validation/salvage/BOOT-04/report.md) | Independent review reproduced host/Chrome identity and confirmed all required/unprovisioned scope; no runtime or funded-staffing claim |
| DATA-01 | Complete / simulation_production; root integration; independent review lego_gameplay | `src/game/construction/construction_types.*`, `tests/test_construction_types.cpp`, `web/salvage_data.mjs`, `scripts/test_salvage_data.mjs`; Bazel/CMake registered | [Validated foundation](docs/validation/salvage/DATA-01/README.md) | 13 sanitizer tests, 13 integrated CTest cases, Bazel construction target, 6 Node cases pass; independent review found no defects. Later build/save/authority contracts remain unchecked |
| DATA-02 | Complete / simulation_production; independent review lego_gameplay; root integration/final fix review | `src/game/construction/part_catalog.*`, `tests/test_part_catalog.cpp`, task evidence; root owns shared build files | [Catalog evidence](docs/validation/salvage/DATA-02/README.md) | 16 optimized Bazel, 16 refreshed sanitizer and 16 final CMake cases pass; immutable owned catalog, bounded authoring validation and exact asset resolution; optimized compiler failures preserved and fixed without suppression |
| ASSET-01 | Complete / root; reviewed by lego_gameplay and render_architecture | `tools/salvage_assets/`, `data/salvage/authoring_probe/`, task evidence | [Authoring evidence](docs/validation/salvage/ASSET-01/report.md) | Clean Blender runs produce identical GLB and preview pixels; tall variant fits; overwrite rejected with unchanged outputs; MCP absent and optional; no runtime-art claim |

| DATA-03 | In progress / lego_gameplay; root integration later | `src/game/construction/build_model.*`, `tests/test_build_model.cpp`, task evidence; no shared build edits until G00 checkpoint | `docs/validation/salvage/DATA-03/` | Canonical sparse build graph, exact socket/solid occupancy and design-only duplication; depends on completed DATA-02; unfinished source stays outside G00 checkpoint |
| ASSET-02 | In progress / lego_gameplay; root schema review/integration | New metadata schema/cooker files under `tools/salvage_assets/`, new isolated fixtures and task evidence; no shared build or existing authoring-probe edits before G00 | `docs/validation/salvage/ASSET-02/` | Bind stable gameplay metadata to exact GLB bytes and preserve it through cooking; validate against DATA-02 contracts; unfinished work stays outside G00 checkpoint |
| LOOK-01 | Prioritized / render_architecture investigates; implementation awaits asset prerequisites | Read-only converter/mesh/render investigation, then actual cove composition and native/browser capture; no visual completion claim | `docs/validation/salvage/LOOK-01/`; preliminary work excluded from G00 | Added from owner's 2026-09-07 feedback that the preview looks worse; first serious visual proof moves earlier than Phase 8. BOOT-05 is explicitly a technical placeholder. |

For each completed task, record the exact tested revision/content identity, commands, actual results, screenshots/captures where relevant, hardware and settings, limitations, and reviewer. For a blocked task, name the missing capability/input and what independent work can proceed. Keep generated media provenance with its assets and durable architectural decisions in this file or repository specifications linked from the task evidence.
