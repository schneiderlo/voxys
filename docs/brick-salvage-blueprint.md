# Voxys: build, explore, salvage

**A game blueprint grounded in the current code**  
7 September 2026 · Review baseline: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`

## Read this first

**Build a machine you care about. Take it somewhere difficult. Bring something useful home.**

My recommendation is a coastal construction adventure. Players build salvage craft, explore abandoned industrial islands, recover physical machinery, and rebuild their home harbor. Cargo weight, attachment points, buoyancy, damage, and route choice make each design matter.

The defining moment should be: **“My machine nearly failed. I understood why. I changed it. It worked.”**

Voxys has substantial engine foundations. Its LEGO game layer is still a small tower-building challenge. The next milestone should be one complete, enjoyable salvage expedition using a machine the player changed.

The central technical decision is to separate **visible parts**, **mechanical assemblies**, and **terrain**. A boat can show hundreds of bricks while simulating a handful of rigid bodies.

The central production decision is to prove one small area at final quality before multiplying content. AAA ambition requires art direction, tools, animation, sound, accessibility, reliability, and human playtesting alongside excellent shaders.

### Reading guide

- For the game: read sections 1–3.
- For the engine: read sections 4–9.
- For the work plan: read sections 10–12.
- For evidence: use section 13.

**Status labels:** “Current” means traced in source or recorded evidence. “Proposed” means new work. Numerical budgets below are initial design constraints, not measured capabilities.

This investigation read the full root `AGENTS.md` and `README.md`, and traced the relevant code with three parallel investigations: rendering; LEGO gameplay; physics, networking, and production infrastructure. It also inspected the stored gameplay captures. It is an architectural review, not a fresh full build, hardware certification, or human playtest. The shared LEGO shader consistency check passed during this review.

## 1. The game worth building

### Player fantasy

You run a small salvage workshop on a damaged coast. You turn recovered machinery into increasingly capable vehicles and restore the harbor that supports your expeditions.

Start with boats. Expand into amphibious haulers and mobile cranes once the first craft is enjoyable. Each new mechanism should unlock a new way to solve a physical problem.

An original utility robot is a promising player character: expressive, visibly mechanical, and compatible with a tactile construction-toy world. That is an art proposal, not a requirement inherited from the code.

### The loop

```text
Choose a recovery job
        ↓
Build or modify a machine
        ↓
Explore and reach the wreck
        ↓
Detach, lift, secure, or tow cargo
        ↓
Adapt to weight, damage, and the return route
        ↓
Bank the haul → restore the harbor → unlock new possibilities
        ↺
```

The return journey is essential. A boat that reached the wreck empty must now bring home a heavy, awkward load. That creates a second problem without needing a second map.

### Four design pillars

| Pillar | What the player experiences | What it requires |
|---|---|---|
| Useful creativity | A wider hull, different winch location, or cargo cradle solves a problem | Build-dependent mass, buoyancy, torque, and attachment forces |
| Readable failure | A stressed connector creaks, a pontoon dips, a cable pulls the bow around | Clear force feedback and consistent structural rules |
| Discovery with purpose | A visible wreck suggests a tool or route worth trying | Authored landmarks, distinct recovery problems, meaningful rewards |
| Ownership and recovery | The machine has a name, a history, and survives mistakes | Blueprints, saves, repair, rescue, and visible harbor progression |

Building vehicles and exploring already overlap with [Trailmakers' stated proposition](https://www.playtrailmakers.com/). The proposed distinction is a tightly authored **salvage-and-recovery experience**: cargo changes handling, damaged machines remain useful, and bringing a haul home changes the harbor. This is a differentiation hypothesis to test with players.

### Progression without busywork

Use two reward types initially: general recovered material, and special machinery that unlocks a capability. Avoid a large resource taxonomy before it adds interesting choices.

Early progression might move from towing to lifting, then stabilizing heavier loads, then cutting structures, and finally operating in more demanding water. Sidegrades should remain useful: a broad stable barge trades speed and access for lifting capacity.

Keep starter blueprints free to restore. Expensive cargo can be lost, but failure should not delete the player's design or trap them in resource grinding. A rescue action returns the player to a safe workshop; significant lost cargo can remain as a marked recovery opportunity within defined persistence limits.

Creative mode should reuse the same builder with unrestricted resources. The expedition mode supplies purpose and consequences.

### Proposed long-term product envelope

These are sizing assumptions for later costing, not commitments:

- A premium PC game with controller and mouse/keyboard support.
- Solo play as a complete experience; a path to 1–4 player co-op after a two-player proof.
- Four compact, authored coastal regions, each with distinct physical recovery problems.
- Roughly 50–70 functional part definitions, supported by cosmetic variations.
- A 12–18 hour authored progression path, with replayable recovery jobs and creative building.
- Boats first; amphibious craft and lifting machinery expand the same core systems.
- Console feasibility assessed after the renderer, input, memory, and save contracts stabilize.

Desktop WebGPU remains a valuable playable demo and engineering target. Prove its production content and session budgets before promising the entire commercial game in a browser. Mobile is a separate platform decision.

The existing WRECKWATER contract describes 12-player competitive salvage. This blueprint proposes an expedition direction in response to the user's preference. It does **not** silently rewrite that contract or claim the existing multiplayer game is complete. Preserve the current prototypes while testing this direction. See evidence E1.

## 2. First prove one memorable expedition

### “The stranded generator”

The player receives a half-built salvage skiff. They add pontoons, an engine, and a winch. A nearby wreck contains a generator needed to power a harbor lift.

Pulling the generator from a high, off-center attachment can make the skiff list when the load exceeds its stability. A good initial design must be allowed to succeed immediately. At least two approaches must work.

At the wreck, the player can adjust winch length, approach direction, and load position. Widening the hull or moving a mounted winch happens back at the workshop, with a quick return/relaunch flow. This does not require an unrestricted builder on a moving boat.

On the return trip, the heavier craft handles differently. Banking the generator powers the harbor lift and unlocks the next recovery tool. The payoff is visible in the world.

### Two levels of proof

**First playable proof:** a fixed starter chassis with editable module sockets. This is sufficient to test meaningful building, load handling, recovery, and reward. Fixed sockets simplify topology and collision cooking; module placement must still change center of mass, inertia, flotation, and force application. That requires extending the present body description. It avoids making arbitrary construction a prerequisite for testing the game.

**Final-quality vertical slice:** extend that journey to true connected assemblies, one limited cutting/breakage case, saved blueprints, a polished workshop, authored surroundings, and finished sound and visual feedback.

The generated crane concept represents the later slice. The first proof can use a fixed winch mast; a powered articulated crane depends on new joint support.

### Final-quality slice contents

This table describes the polished slice. The first module proof uses placeholder art, a fixed winch mast, and restricted editing while retaining the physical load response, persistence, reward, and recovery loop.

| Item | Proposed scope |
|---|---|
| Location | One harbor, one cove, one wreck, a few minutes of travel |
| Session | A 15–20 minute first expedition, including building |
| Parts | About 12 definitions: beam, plate, pontoon, engine, propeller, helm, winch, tow eye, cradle, brace, ballast, repair module |
| Machine | One player craft; one alternate starter design |
| Cargo | One persistent generator; a second short job with an awkward heavy crate using the same cove and mechanics |
| Failure | One attachment overload or detachable float; clear rescue and retry |
| World | One authored weather state; local water interaction |
| Progression | Generator powers the harbor lift; a follow-up haul tests the revised craft and rewards general material |
| Persistence | Blueprint, inventory, mission state, and significant cargo |
| Player | Workshop camera, chase camera, enter/exit helm, simple dock movement |

Free walking on a rolling boat is a later slice gate if it is part of the intended experience. The first proof can use a helm interaction and safe dock transitions. Do not present this shortcut as completed moving-platform character support.

The player enters the helm through one contextual action, highlights a compatible cargo tow eye, attaches the line, reels it in or pays it out, and releases it deliberately. A separate confirmed latch secures cargo in a cradle; approach, attach, reel, latch, and release must all work with both a controller and mouse/keyboard.

### Human test gate

Run an initial test with 8–12 people, including players unfamiliar with construction games. This is a diagnostic sample, not market validation.

Proposed targets:

- At least 80% launch within three minutes without spoken instructions.
- At least 70% complete a haul without developer intervention.
- At least 60% make a deliberate design change and can explain its effect.
- At least half voluntarily start another expedition when allowed to stop.
- No critical save loss, progression blocker, or unrecoverable vehicle failure.

Record what people actually do, where they hesitate, and which failures they understand. A small sample can reveal problems; it cannot establish retention or commercial demand. If players simply follow the starter blueprint, revise the mission or builder before adding regions.

## 3. What the code gives us today

| Area | Current capability | Missing for this game |
|---|---|---|
| Application | Shared C++ lifecycle; native and WASM launchers; multiple experiences | A clean expedition session boundary |
| Landscape | 8192² heightfield; hierarchical ray traversal; shared stepped studs | Editable ruins and authored gameplay density |
| LEGO layout | Deterministic 32² grouping; fixed 4 MiB toroidal material cache | Authored editable salvage assemblies |
| Build & break | 48 loose bricks, 8 balls, three brick shapes, target challenge | Connections, vehicle modules, useful build operations |
| Physics | GPU bodies, contacts, CCD, sleep, queries, events, powered rope | General compounds, authored inertia, hinges, motors, suspension |
| Rendering | GPU pose consumption; ocean FFT; terrain caches; primitive and mesh paths | Unified dynamic lighting/water composition and stable temporal output |
| Structural model | Graph damage, connected components, mass, volume, center of mass | Live build edits, shape cooking, bounded production histories |
| WRECKWATER | Native authority/client code, cargo/damage/replay infrastructure | General user-built craft and a shipped browser co-op session |
| Content | glTF-to-VMESH tooling and material/skin/animation data structures | A validated part/mission authoring workflow |
| Persistence | WRECKWATER replay infrastructure | LEGO blueprints, workshop inventory, crash-safe expedition saves |

Evidence: E2–E10 and E14 below.

### Three distinctions that change the plan

**The terrain is a heightfield.** Grouped brick ownership controls seams and color. It does not make the columns removable bricks or give the world caves and overhangs.

**The construction bricks do not clutch together.** They rest on each other's studs. Their current vertical stack pitch is 0.96 + 0.18 = 1.14 units. A production socket connection needs an explicit insertion depth and build lattice; inheriting that stacking distance would encode the wrong behavior.

**The renderer is GPU-resident; gameplay is not free of synchronization.** Direct rendering avoids a transform readback for each visible brick. Authoritative scoring, events, persistence, and networking still need an explicit path to completed simulation state.

The main browser route now selects `lego-world`, even though parts of the README still describe the older defaults. Likewise, some WRECKWATER documents lag its native application wiring. This review gives source code priority when those descriptions disagree. See E1 and E8.

## 4. Target architecture

**Decision: add a canonical game/build model above the existing engine, with commands as the boundary.**

### Keep the engine; make the game explicit

Avoid a broad rewrite. Extract new responsibilities as the first expedition needs them. `Application` should coordinate the frame, not accumulate construction, inventory, mission, and network rules.

Proposed modules; these paths do not exist yet:

```text
src/game/construction/
  part_catalog         immutable definitions and validated assets
  build_model          persistent parts, sockets, connections, revisions
  build_commands       place/remove/configure/undo transactions
  assembly_compiler    shapes, mass properties, joints, render mappings

src/game/expedition/
  game_session         fixed-tick authority and accepted commands
  expedition_state     objectives, recovery, launch, return
  salvage_inventory    ownership, rewards, costs, banking
  save_store           checkpoints, journal, migration

src/render/
  assembly_path        part instances driven by rigid roots
  scene_composition    shared opaque scene, water, effects, output
```

Keep rendering and platform objects out of the canonical build/save model. Use the same gameplay command boundary in local solo play and a later host-authoritative session.

```text
Input / builder UI
       ↓ commands with sequence + expected revision
GameSession ───────────────→ save journal / later replication
       ↓ accepted edits
BuildModel → AssemblyCompiler → physics shapes + joints
       │                              ↓ fixed ticks
       └── part-to-root map ───→ GPU root poses
                                      ↓
                              assembly rendering
```

This is a proposed boundary. The existing application schedules physics from update/render and does not already provide this construction session.

### Canonical data

| Record | Data |
|---|---|
| Part definition | Asset ID/version; lattice footprint; allowed orientations; mesh/material; collision proxy; mass/COM/inertia; buoyancy regions; typed connector frames; module parameters; cost |
| Part instance | Durable ID; definition ID; local integer position; discrete orientation; paint; health; configuration |
| Connection | Durable ID; part/socket endpoints; connection type; strength; damage; enabled state |
| Build | Durable ID; revision; parts; connections; module settings; ownership |
| Compiled assembly | Derived shape data, rigid roots, joints, mass properties, lookup tables; rebuildable, not authoritative save data |

Do not save transient physics `BodyHandle` indices, GPU addresses, or layout-cache tags. Persist logical identities and reconstruct runtime handles.

Keep build coordinates integer. Choose and document one physical scale before balancing engines, mass, gravity, camera height, and water. Preserve the present unit convention during the first proof; perform any metres-per-stud conversion once in the authoring/compiler boundary and validate all consumers together.

### Builder transactions

Preview placement immediately against the build graph and socket occupancy. The current 10 Hz debug snapshots are not an appropriate authority for responsive workshop editing.

An accepted edit atomically validates the build revision, compatible sockets, overlap, bounds, inventory, permissions, and capacity. Either the whole edit happens or none of it does.

Start with add, remove, rotate, configure, undo, redo, save, and duplicate. Add symmetry and multi-select when the basic builder works. Major topology edits happen while docked and stable. Field repair can initially replace or patch a damaged module through a restricted action.

Undo must reverse inventory and topology together. In co-op, undo becomes an authorized compensating command against the current revision; it must not roll another player's accepted work backward.

## 5. Machines, physics, and structural failure

**Decision: simulate welded parts as rigid clusters. First skiff: one rigid hull, local thrust, and one winch; general articulation comes later.**

### A machine has three representations

1. **Part graph:** what the player built, owns, configures, and repairs.
2. **Mechanical graph:** rigid clusters and the joints between them.
3. **Presentation:** visible parts, materials, lights, ropes, sound, and effects.

A welded 200-part hull should normally be one rigid body. A rotating crane adds bodies for its moving links. Cargo is independent until its restraint model says otherwise. A broken-off pontoon becomes independent only when connectivity changes.

This reduces solver work while preserving detailed appearance:

```text
part world transform = interpolated rigid-root transform × part-local transform
```

Logical socket bonds define connectivity. Do not simulate frictional interference inside every stud/socket. Render the mating geometry correctly, but cook collision proxies that omit internal contact surfaces.

### Assembly compilation

For each welded connected component:

- Identify exterior collision proxies and build a child acceleration structure.
- Combine mass, center of mass, and inertia.
- Create buoyancy regions and module force application points.
- Build joint anchors in the correct body-local frames.
- Produce persistent-part-to-rigid-root and collision-feature-to-part mappings.
- Cache the result by topology and content version.

For part masses `mᵢ` and centers `cᵢ`:

```text
M = Σ mᵢ
C = Σ(mᵢ cᵢ) / M
rᵢ = cᵢ − C
I = Σ [Rᵢ Iᵢ Rᵢᵀ + mᵢ ((rᵢ·rᵢ) Identity − rᵢ rᵢᵀ)]
```

`Iᵢ` is each part's inertia about its own center. `Rᵢ` rotates it into the assembly frame. This is the parallel-axis construction. Using only the enclosing box's inertia would lose the effects of the player's weight distribution.

Expose a full tensor in the shape/body contract, or compute principal axes and retain the transform between build frame and physical body frame. The current primitive spawn description cannot express this generally. See E5.

Rigid assembly changes also need a defined conservation policy. Removing a part during free motion transfers its share of momentum to the new body. Adding parts from a dock inventory is an explicit workshop operation; it should not become an unlimited propulsion exploit.

### Joints in the right order

The current powered, tension-only distance attachment is a useful winch foundation. It does not supply a general articulation system.

Implement only what the next machine needs:

| Stage | Mechanical support |
|---|---|
| Starter salvage skiff | Welded modules, thrust at local points, winch, cargo restraint |
| Crane slice | Hinge with limits, motor target/speed, force cap, break evidence |
| Amphibious hauler | Wheel contact, suspension, steering, traction, drivetrain |
| Later tools | Slider/piston and specialist mechanisms justified by missions |

Express tuning in meaningful units: force/torque caps, speed, frequency, and damping where appropriate. Keep fixed ticks and a measured substep policy. Test long lever arms, high mass ratios, grounded hulls, winch slack-to-taut transitions, and simultaneous joints/contact constraints.

### Choose the solver using a machine workload

**Recommendation: retain WebGpuSoft for the first skiff proof, then make a measured decision before committing to general articulation.**

The existing GPU route has valuable direct rendering, contact, event, and transaction infrastructure. But loose-body throughput is not evidence of crane or vehicle stability. One documented 100,000-body sparse GPU scene had zero dynamic contacts and p95 24.685 ms; other fixtures test different contact patterns. None substitutes for the intended machine scene. See E11.

Time-box an architecture spike around a loaded winch and one powered crane. Compare extending WebGpuSoft with adapting a CPU solver through the same machine interface if the GPU approach misses stability or development-cost goals.

This is **not a drop-in backend toggle**. Existing CPU adapters do not expose the complete LEGO terrain, authored compound, event, stable lifecycle, and atomic mutation contracts required here. Jolt has some stable spawn support; that does not make the full gameplay path interchangeable. See E5.

Jolt's own architecture describes the different query/build tradeoffs of static and mutable compound shapes. It is a useful implementation reference, not proof that this repository already integrates those features. [Jolt architecture](https://jrouwe.github.io/JoltPhysics/)

A CPU-authoritative machine path could coexist with cosmetic GPU debris. Every authoritative body must still have exactly one solver owner. Cosmetic debris must not push authoritative machines through an unimplemented cross-solver coupling. A CPU path would use compact render uploads; document that tradeoff instead of claiming it retains zero pose uploads.

### GPU execution details

**Separate work by behavior.** Keep frequently accessed poses, velocities, mass properties, and solver data compact. Keep catalog text, authoring metadata, and other cold data elsewhere. Use contiguous arrays and stable indices; verify actual access patterns before choosing packing.

**Exploit independent machines and independent constraint sets.** Graph coloring can create parallel sets without shared body writes. High-degree nodes and uneven color batches limit scheduling efficiency. Long chains, closed loops, and high mass ratios also stress iterative convergence, even when a graph needs few colors. Test scheduling cost and stability separately. Preserve a consistent solve order and warm-start identity through unchanged topology.

**Compact awake work.** Dispatch active bodies, pairs, and constraints where useful. Sleeping machines should avoid most solver work. Wake propagates through mechanical dependencies when a motor, contact, load, or edit matters.

**Fix the large-body case.** The current broad phase scans all resident bodies for oversized objects. Aggregating a hull reduces body count but may increase candidate work. Benchmark mixed small debris and large craft. Add a separate large-object path or hierarchical broad phase, plus acceleration within compound shapes. Merely increasing the uniform grid size is not a complete answer. See E12.

**Bound contact generation.** The playground can examine up to 81 child pairs for two studded bricks. General vehicles need exterior proxies and spatial pruning. Keep contacts spread across relevant support regions; a single four-point manifold across an entire long hull can lose stability. Define per-subpair and per-assembly budgets with visible overflow diagnostics.

**Avoid an all-features shader.** The prototype already encountered a software-driver failure associated with reachable generic compound-shader complexity. Maintain specialized collision classes and a portable baseline. Profile registers, local memory, bandwidth, occupancy, and total pass time. Higher occupancy alone does not guarantee faster execution. [AMD GPUOpen](https://gpuopen.com/learn/occupancy-explained/)

**Do not assume a subgroup is a fixed SIMD width.** Feature-gate subgroup optimizations and preserve a baseline path for the pinned toolchain. WGSL does not guarantee a mapping from subgroup IDs to local invocation indices. [WGSL specification](https://www.w3.org/TR/WGSL/)

**Measure barriers and dispatch cost.** Fuse operations only when dependencies, register use, and memory traffic justify it. Dependent simulation and rendering on a shared WebGPU queue do not automatically become concurrent because the algorithms are parallel.

### Breakage must be a transaction

The existing `StructuralAssembly` supplies useful component reconstruction and damage ordering. Its fixed-point fixtures are not already a live, editable assembly compiler; histories also need explicit bounds. See E6.

For the first slice, use a tunable structural model:

- Winch tension and impacts produce loads at known part locations.
- Loads are distributed to nearby/connected bonds using a documented approximation.
- Stress warnings precede failure when gameplay allows it.
- Broken bonds trigger connected-component reconstruction.

A welded component has no internal joint reaction impulses to read. Do not pretend every bond's stress is available from the rigid-body solver. Start with a bounded load-distribution rule; consider a coarse graph elasticity solve only if the game needs better structural behavior. Full finite-element mechanics is not required for the first slice.

On an accepted split, update connectivity, collision, mass/inertia, body ownership, joint endpoints, render mappings, mission ownership, and journal state under one topology revision.

Prepare resources before committing. Keep the previous revision valid while asynchronous work is pending. Never render a part as detached while collision still treats it as welded. Use stable feature IDs or invalidate cached contacts when topology changes.

For a fragment center offset `r` from the parent center:

```text
v_fragment = v_parent + ω_parent × r
ω_fragment = ω_parent
```

Initialize fragments from the parent's post-solve rigid velocity field and preserve momentum across the full split. Do not reapply an impact already included in those velocities; apply only new impulses afterward. Check mass and energy accounting numerically. When no significant-fragment slot is available, defer the split or use a predefined coarse fragment representation. Never discard valuable cargo or secretly turn a functional engine into cosmetic debris.

### Water that makes building matter

Use a bounded set of sealed pontoon/hull volumes first. Estimate submerged volume and apply buoyancy at each region's submerged center:

```text
buoyant force magnitude = water density × gravity × submerged volume
torque = offset from assembly center of mass × force
```

Apply drag from the body's velocity relative to local water, including angular velocity at the sample point. Apply propulsion at the propeller mount so placement affects handling. Avoid counting overlapping buoyancy volumes twice.

Loading cargo changes combined mass distribution through actual contact/restraints or an explicitly latched compound state. Do not add cargo mass to the hull while also simulating the same cargo independently.

Later, compartments can flood and lose effective flotation. Define one consistent model for displaced exterior water, interior water mass, and open volumes so flooding is not double-counted. Full fluid slosh can remain outside the initial model.

Keep the spectral ocean for broad waves. Add local wake, propeller foam, splash, and wetness effects around active craft. Fluid simulation across the entire ocean is unnecessary.

The current GPU consumes the full FFT surface; CPU consumers use a strongest-24-mode approximation plus long swells. Define a `WaterField` contract with simulation tick, height, normal, velocity, and error tolerance. Drive authoritative forces from fixed simulation time. Rendering can interpolate; a presentation clock must not change the gameplay sea state. See E7.

## 6. Rendering a coherent game world

**Decision: put terrain, machines, and salvage into one opaque scene before water, then stabilize the complete image.**

### Repair composition before adding spectacle

The current order is approximately:

```text
Water simulation → GPU physics
→ terrain visibility and terrain/sky/water presentation
→ dynamic primitives → optional meshes
```

Dynamic construction arrives too late to participate in the water's opaque-scene refraction. Water writes depth; the later primitive shader rejects submerged fragments behind it. Dynamic objects also lack the same shadow/environment integration as the terrain. This is a concrete obstacle for visible underwater salvage. See E3 and E4.

Proposed frame order:

1. Advance authoritative simulation and required water fields.
2. Cull assemblies; generate shadow visibility and draw lists.
3. Produce shared terrain/object shadow inputs.
4. Resolve terrain visibility and static lighting into an opaque HDR scene.
5. Draw machines, ruins, cargo, and characters into that same HDR scene/depth.
6. Composite ocean using the complete opaque scene for refraction and supported reflections.
7. Resolve underwater medium, glass, ropes, spray, and other transparency according to an explicit ordering policy.
8. Apply temporal reconstruction, exposure, and one output transform.
9. Draw readable UI.

A small explicit frame graph is enough. Specify resource ownership, reads/writes, lifetime, depth convention, and invalidation. Retain static terrain caches, but separate static lighting from moving shadows so caching never freezes a boat's shadow.

Preserve opaque HDR color and sampled depth as immutable water inputs, and composite water into separate output attachments. Do not sample a texture subresource while simultaneously binding that same subresource as the render attachment. Make these lifetimes explicit in the frame graph.

Add bounded water-exclusion masks or volumes tied to hull interiors. Shared opaque depth/refraction alone does not prevent the ocean surface appearing inside a floating open boat. Flooded compartments must update interior water presentation and exclusion consistently with the gameplay model.

Reflections can begin with environment lighting plus a bounded screen-space path and fallback. An object being in the opaque scene enables refraction; it does not automatically produce every reflection or off-screen effect. Choose those effects deliberately.

### Assembly rendering

Cull rigid-root bounds first. For surviving roots, cull or select LOD for spatial part clusters. Bin instances by mesh, material class, and LOD, then issue indexed indirect draws with reusable resources.

Upload changed topology, local part transforms, material assignments, and root mappings. Read moving root poses from resident physics buffers on the GPU path.

Use dedicated part meshes or variants instead of executing unused stud vertices for every brick. The current shared box/eight-stud draw is sensible at 48 bodies; it is not the final strategy for thousands of visible parts.

### Temporal quality

The current distant roughness/seam fading helps shading, but subpixel studs and silhouettes can still sparkle. The stored captures acknowledge this.

Add camera and object motion vectors, previous/current root transforms, jitter-aware terrain rays, and temporal rejection using depth, stable IDs, and topology revisions. New fragments and edited parts need history invalidation. Water and foam need reactive handling so they do not leave trails.

Separate **unjittered cache identity** from temporal sample state. The existing ray cache compares camera uniforms; inserting jitter blindly can force a full terrain refresh every frame. Either retain a valid unjittered visibility cache with a defined reconstruction method, or budget jittered retracing where required.

Use screen-footprint LOD for fine studs, seams, and bevels. Preserve the broad silhouette and useful socket cues. Keep a sharp reference mode to compare against temporal blur and ghosting.

### Art direction

Aim for a tactile miniature world: slightly scuffed plastic, clear mechanical silhouettes, restrained color families, soft environmental light, and convincing water.

The current landscape covers a large area, but an authored game needs destinations: a harbor, a broken crane, a stranded ferry, a sea arch, a storm barrier. Each landmark should offer a physical problem and remain recognizable from water level.

Use broad color masses to communicate function: flotation, structure, power, tool, and cargo. Pair color with shape/iconography. Keep surface wear quiet enough that connection points and damage remain readable.

The generated storyboard explores workshop → loaded salvage → homecoming. It is concept art, not an engine capture. Its dense detail, crane, harbor lighting, and character are targets to evaluate, not implemented assets.

![Exploratory workshop, salvage, and return storyboard](/home/modkin/workspace/schneiderlo/voxys/docs/concepts/build-explore-salvage.png)

Generated with the built-in image tool. [Exact prompt](/home/modkin/workspace/schneiderlo/voxys/docs/concepts/build-explore-salvage-prompt.md). The tool did not expose a selectable model version, so this document makes no “Image 2” model claim.

## 7. World, content, and memory

### Three world layers

| Layer | Representation | Interaction |
|---|---|---|
| Landscape | Existing heightfield and analytic brick treatment | Walk, drive, ground, collide |
| Salvage sites | Authored persistent part assemblies | Cut, detach, recover, repair |
| Machines and debris | Rigid roots, joints, visual part instances | Build, move, carry, break |

This gives the game editable material where it matters without expanding every terrain cell into a physics object.

If excavation later becomes central, introduce bounded editable volumetric regions or a separate volume representation. A heightfield cannot represent arbitrary tunnels. The render hierarchy, collision data, and saved edits must change together. The 4 MiB material cache is not an excavation system.

### Streaming

The current layout cache streams material ownership. The complete terrain and other shared assets remain loaded. At startup the recorded live WASM allocation is about 404.2 MiB within a 512 MiB heap. That leaves roughly 108 MiB of nominal heap space, before new content, transient peaks, and fragmentation; it says nothing by itself about available GPU memory. See E10.

One 8192² 16-bit image is 128 MiB. A full same-format mip chain is about 170.7 MiB. Treat those as resource arithmetic, not a measured total residency figure.

Before expanding content:

- Package only the selected experience and shared essentials.
- Load region/ruin/texture bundles asynchronously, with bounded staging buffers.
- Track committed and transient CPU, WASM, JS, and GPU memory separately.
- Keep collision and gameplay data available before making an interactive region reachable.
- Keep connected/towed assemblies loaded as a dependency group.
- Use sector-local coordinates for simulation and camera-relative rendering; persist sector plus local position instead of large absolute float coordinates.
- Define unload rules for sleeping machines, valuable cargo, missions, and saved modifications.

Initially, one compact authored cove can remain resident. Real terrain streaming is a later engineering task if the shipped region sizes need it. Do not claim the current material cache already provides it.

### A designer must be able to make content

The existing glTF-to-VMESH path is a useful start, including material, skin, and animation structures. Those structures do not prove a finished character animation system or production editor. See E9.

Build an offline content cooker around an explicit part schema. It should validate units, pivot/orientation, sockets, collision convexity, mass/inertia, buoyancy, material slots, LODs, and dependencies. Generate compact runtime assets with stable IDs and a content digest.

Use compressed texture bundles where appropriate. Khronos KTX2/Basis supports compact distribution and transcoding to supported GPU formats; integrate it as a versioned content change rather than assuming the current VMESH RGBA8 image section already supports it. [Khronos KTX](https://www.khronos.org/ktx/)

Provide a part test scene with placement, articulation, mass, flotation, damage, and render previews. A designer should add a pontoon without changing C++ or WGSL.

Provide an expedition authoring view for landmarks, launch points, cargo IDs, objective predicates, extraction zones, recovery rules, and rewards. Use data-driven conditions such as “cargo is secured inside harbor zone for N ticks.” Mission scripts must not directly overwrite physics state.

Reuse modular site kits, but vary the recovery problem: depth, access width, cargo orientation, attachment availability, required cutting, and return route. Measure designer hours per finished site before estimating four regions.

### Character, UI, and audio

For the slice, finish one character, one helm interaction, one repair/cut action, and transitions between dock, builder, and driving. Add locomotion state, animation blending, tool alignment, and later moving-deck attachment/velocity handling. A rigid robot reduces some skinning demands but still needs animation and a readable camera.

The workshop needs orbit/pan/zoom, instant testing, undo, clear connection previews, named blueprints, and short placement-failure explanations. Optional center-of-mass and flotation markers should help players diagnose a build.

Use large visual part categories, short labels, adjustable text, remappable inputs, controller navigation, captions, and reduced motion. Never rely on color alone or long tutorial paragraphs. Test these controls with dyslexic players and people using assistive settings.

Build sound from physical events: engine load, winch strain, plastic creaks, impact material, water entry, and successful latching. The current browser oscillator clicks are a prototype. A production audio mixer needs bounded voices, prioritization, spatialization, and native/browser parity. Haptics and camera motion should reinforce force without obscuring control.

## 8. Saves, authority, and eventual co-op

**Decision: make solo play authoritative and durable locally; reuse that boundary for a measured two-player session later.**

### Solo first, through an authority boundary

Run `GameSession` locally for solo play. UI submits commands; the session owns accepted edits, inventory, cargo banking, and mission transitions. Rendering is a view of that state.

The production float GPU world reports that cross-platform float determinism and lockstep are unavailable. Separate fixed-point fixtures do not change that. Store authoritative outcomes and checkpoints where needed; do not promise exact replay from input alone. See E8.

### Durable saves

Persist catalog/schema versions, blueprints, inventory, world discoveries, mission progression, significant cargo IDs, machine topology, and relevant motion state. Include joint state, motor targets, rope lengths and ownership, damage/flooding, machine resources, simulation time, procedural seeds, a journal sequence, and integrity checks.

The initial guarantee is semantic resume: restore the same meaningful machine, cargo, and progression state with a controlled settling transition. Exact physical continuation would additionally need certified solver/contact history and compatible versions; it is a separate requirement.

Write a checkpoint plus a bounded operation journal. Native storage needs crash-safe replacement; browser storage needs a committed IndexedDB transaction and user export/import. Handle quota failure explicitly. Choose storage durability and retention policies before calling browser saves reliable.

Banking is one transaction: remove expedition ownership, add the reward, and complete the objective once. A cargo ID cannot be both banked and still collectable after reconnect or reload.

GPU-backed saves capture one certified completed simulation tick. Do not combine topology from tick N with poses/events from N−k. The current HUD snapshot path is not a save contract.

Test interruption before and after every commit boundary, missing part definitions, duplicate IDs, migrations, journal truncation, and reload with detached cargo. Preserve the last known-good save if a new version fails validation.

### Two-player proof before four-player ambition

Reuse the repository's authority, command validation, replication, and replay patterns. Generalize the data model deliberately: the current WRECKWATER authority has a four-peer, three-main-body/eight-fragment scope. It is not a general machine server. Its native application path is wired; the corresponding WASM bootstrap explicitly rejects that native transport. See E8.

Use reliable ordered messages for topology, inventory, and session events. Use bounded sequenced state updates for motion. Replicate rigid roots and joint state rather than each visible brick pose. Snapshot interest must include rope endpoints, cargo, and connected machinery together.

Every motion snapshot carries an authority epoch and topology revision. Unknown revisions enter a bounded pending queue; clients request missing topology or a full baseline. A revision becomes visible only when its mappings and matching poses are ready. Separate network channels do not guarantee ordering across each other.

An initial candidate is a 60 Hz authoritative simulation with 20 Hz root snapshots. Measure and tune this. Local input prediction and correction need special treatment for occupied/towed machinery; a player cannot predict their boat as if the connected cargo were absent.

For scale intuition only: 64 roots × an assumed 64-byte motion record × 20 updates/s is 81,920 bytes/s per recipient before headers, topology, redundancy, and other state. Compression can reduce it; congestion and bursts still need testing.

Define a bounded GPU evidence/readback frontier for server decisions and snapshots. Direct GPU rendering does not remove readback cost for the host. Measure input-to-authoritative-action latency and correction quality, not only server tick time.

Public co-op requires encrypted transport, admission, session discovery, reconnect, and a tested hosting model. Start with one host authority per expedition. Defer seamless host migration and distributed world ownership; the existing distributed fixed-point fixtures do not supply those features for arbitrary GPU machines.

The co-op gate is two rendered clients completing a real haul under 100 ms RTT, jitter, 2% loss, duplication, and reordering. Test actual transport behavior as well as deterministic frame adversity. Disconnect/reconnect must preserve cargo and inventory exactly.

## 9. Performance budgets and engineering gates

### What has actually been measured

The stored 7 September captures report the following on an AMD Radeon 890M, Linux/Wayland, Chrome 152, at 1920×1080. These were short scene samples; they are not new measurements from this review.

| Workload | GPU p50 / p95 | Presentation cadence p50 / p95 |
|---|---|---|
| Walking | 6.69 / 7.54 ms | 17.70 / 19.30 ms |
| 48 bricks + 8 balls | 4.36 / 4.60 ms | 18.70 / 23.30 ms |

Source: E10. Presentation cadence is measured from browser animation callbacks, not a photodiode measurement of pixels appearing on screen.

The historic approximately 945 FPS offscreen/cached throughput result is useful engine evidence. It does not establish the product's frame rate. The short moving captures already exceed a 16.67 ms p95 cadence target. Investigate presentation pacing, queue depth, browser scheduling, and input latency alongside GPU cost.

### Proposed slice limits

These are initial hard limits to make the work measurable. They are deliberately separate from today's 48 loose-brick cap and from the eventual game's capacity.

| Resource | Initial slice budget |
|---|---|
| Parts per player craft | 256 logical parts |
| Total significant rigid roots | 64, including craft links, cargo, and fragments |
| Active mechanical joints | 128, counting ropes and cargo restraints |
| Detailed visible part instances | 8,192 across craft and authored sites |
| Significant salvage items | 32 per loaded expedition |
| Cosmetic debris | 512 pooled instances, distance/lifetime limited |
| Edited structures | 8 simultaneously resident assemblies |
| Save journal | Bounded by bytes and operations; checkpoint before the chosen limit |

These limits require implementation and profiling. They are not extrapolations from the existing benchmark. Derive pair/contact/command capacities from worst-case scenes; count overflow and reject or defer new work rather than silently dropping it.

### Frame and tick targets

Use the 890M as the first reproducible low-end reference because evidence already exists for it. Add explicit AMD, Intel, and NVIDIA reference systems before calling support broad. Record CPU, memory, driver, power mode, display rate, resolution, and preset with each capture.

An initial full GPU-frame allocation at 1080p:

| Work | Target ceiling |
|---|---:|
| Machine/contact simulation | 3.0 ms |
| Terrain visibility and static scene work | 2.5 ms |
| Dynamic geometry and shadows | 2.5 ms |
| Ocean simulation and composition | 2.0 ms |
| Effects, temporal resolve, output | 2.0 ms |
| GPU contingency | 1.5 ms |
| **Total design envelope** | **13.5 ms** |

This is a planning allocation, not a demonstrated fit. The current renderer will need remeasurement after composition changes. Stage percentiles cannot simply be added to infer a frame percentile; measure the complete frame interval as well.

Target CPU gameplay/encoding work below 4 ms p95 on the chosen reference. CPU and GPU costs may overlap; do not add them blindly or assume all overlap is free. Profile topology rebuilds, allocations, draw submission, save staging, and browser UI work.

The product gate is 60 Hz simulation plus p95 presented cadence within 16.67 ms under the agreed measurement tolerance. Track p99 spikes and frame misses as separate release metrics. Aim for p99 below 25 ms and no topology/streaming hitch above 50 ms in the scripted slice route. These are proposed thresholds to ratify after baseline capture.

Measure input latency and GPU queue age. The WASM entry currently permits up to 12 in-flight GPU frames as a safety bound. That is not a desirable latency target. Tune pacing against the visible application and compositor; do not force a low queue limit without measuring stalls. See E13.

### Performance scene

Record the whole journey: workshop edit → launch → turning near shore → loaded winch → controlled breakage → cargo return → banking → save/reload.

Include the worst useful machine: long crane, heavy cargo, offset load, nearby debris, moving water, and dynamic shadows. A thousand noninteracting spheres do not exercise those dependencies.

Record frame p50/p95/p99, simulation ticks, GPU stages, root/joint/contact counts, queue age, input latency, allocation/upload/readback bytes, and memory peaks. Perform long sessions with repeated rebuilds, region changes, and recovery. Separate warm steady-state tests from cold startup and shader compilation.

Keep software-driver startup and shader tests. They prove useful correctness/portability properties, not hardware performance. Preserve blocking failures; a retry for diagnostics must not erase a failed result.

## 10. Production path toward AAA quality

### Stage gates

Work proceeds when the previous stage proves its purpose. Time ranges are rough planning assumptions for a small, experienced, full-time team. They are not promises or an estimate for one person working evenings.

| Stage | Deliverable | Exit condition | Rough planning range |
|---|---|---|---|
| A — machine and fun proof | Editable starter hull, winch, physical cargo, return reward | Human players change a build for a reason and want another haul | 4–8 weeks |
| B — complete foundation | Canonical build model, assemblies, saves, meaningful failure, solver decision | Stable load handling; rebuild/save correctness; second expedition works | 8–16 additional weeks |
| C — final-quality slice | One polished harbor/cove, builder, character, audio, water/scene integration | Human, visual, reliability, and visible-frame gates all pass | 12–24 additional weeks |
| D — co-op and content proof | Two real clients; second region kit; designer tools | Consistent haul under adversity; measured content production rate | 8–16 additional weeks, with some parallel work |
| E — funded production | Regions, part families, progression, platform work, QA | Content complete, performance stable, save compatibility protected | Re-estimate from B–D evidence |
| F — release preparation | Onboarding, accessibility, localization, recovery, support, store/build pipeline | Independent QA and external player validation | Budget explicitly in production plan |

A first publishable machine prototype can arrive well before the final-quality slice. Do not equate the earlier milestone with AAA completion.

### Team and cost model

For preproduction, plan around an accountable creative/gameplay lead, simulation engineer, rendering engineer, tools/gameplay engineer, technical/environment artist, and part/character artist. Audio, UI/UX, animation, QA, and production need explicit owners, whether dedicated, shared, or contracted. Roles can overlap; the work still exists.

For full production, expand according to measured bottlenecks: environment/content teams, animation, technical art, tools, gameplay, networking, platform engineering, UI, audio, QA, accessibility/localization, and production. A literal AAA-scale release is a staffed, funded production undertaking. AI assistance does not remove integration, art approval, testing, and player feedback.

Do not invent a total dollar budget from this checkout. Use:

```text
staff cost = Σ(role headcount × months × fully loaded monthly cost)
total = staff cost + outsourcing + hardware/services + QA/platform work
        + localization/accessibility + marketing/support + contingency
```

Use actual rates and outsourcing quotes. Carry an explicit uncertainty reserve; a provisional 25–35% during preproduction is a planning assumption to revisit after the machine and content gates.

If the project remains solo, pursue stage A first and assess a compact commercial game with the same fantasy. Keep the AAA blueprint as the expansion path. Do not announce four regions, broad platforms, and four-player co-op until staffing and measured production rates support them.

### Keep or replace the engine?

**Keep Voxys for the first proof.** It already has the landscape, water, GPU bodies, test infrastructure, and source-level control needed to explore the differentiating experience.

Reassess after the machine/authoring spike. The decision criteria are stable mechanics, time to add a part or mission, finished visual quality, profiling visibility, platform reach, and maintenance cost.

If basic vehicle mechanics or content tools consume most of the schedule, compare one equivalent small scene in an established engine before funding the full custom stack. Do not undertake a migration merely to acquire a renderer feature list. The game-specific build graph, content IDs, and save schema should remain portable whichever runtime wins.

### Content and quality gates

Every new region must introduce a new recovery decision, not only different scenery. Every new part needs a job that demonstrates its value. Every major mechanic needs onboarding, feedback, a failure/recovery path, save behavior, and performance acceptance.

A designer must deliver a new validated part and a complete salvage job without modifying engine source. Capture the actual time from authored asset to approved in-game result. Use that rate to forecast content scale.

Before release, require independent visual review of the real playable route: coherent materials/light, useful silhouettes, no distracting shimmer, stable water intersections, readable damage, and a clear objective. Concept images are references; they do not pass this gate.

Include crash reporting with build IDs, save backup/recovery, graphics/device-loss handling, audio/device changes, controller reconnect, window changes, accessibility settings, and localization layout checks. These are scheduled product tasks with owners, not a final polish bucket.

Public blueprint sharing comes after local export/import. It needs schema validation, part/version compatibility, capacity limits, and content/reporting rules. Public UGC services and live operations are not prerequisites for the solo expedition proof.

## 11. The first implementation backlog

This is the recommended execution order. Work can overlap only where its inputs are stable.

| Priority | Concrete change | Acceptance |
|---|---|---|
| 1 | Add a separate expedition mode and one authored cove | Existing modes still launch; expedition starts at the workshop |
| 2 | Add a small part catalog and editable starter-hull model | Pontoon, engine, and winch positions/configuration are saved data |
| 3 | Add transactional edits, undo, and blueprint save/load | Repeated edits and reload preserve parts and inventory exactly |
| 4 | Compile hull mass/buoyancy and exposed collision | Moving weight changes trim; wider floats change stability; no double mass |
| 5 | Add helm input, thrust, winch, and physical generator | Player can launch, tow, restrain, and bank cargo |
| 6 | Add reward, harbor change, rescue, and second attempt | A complete build → haul → improve loop works through shipped controls |
| 7 | Integrate dynamic opaque scene before water | Submerged generator remains visible through water; shared depth is correct |
| 8 | Playtest the module proof | Apply the human gate; revise before generalizing construction |
| 9 | Generalize sockets, welded assemblies, and one split | Graph/collision/render/save revision remains consistent |
| 10 | Run powered-crane solver spike and choose a route | Loaded articulation meets stability and time budgets |
| 11 | Finish builder, character, sound, art, and temporal stability | Final-quality expedition passes independent review |
| 12 | Add two-player session and content-authoring proof | Two clients bank one cargo exactly once; designer adds the next mission |

Priorities 1–6 form the first playable proof; priority 7 should start in parallel once the test scene exists. General articulation, whole-world editing, more biomes, and public services should not displace that first loop.

### High-value tests

- Build → serialize → load preserves canonical parts, sockets, configuration, and revision.
- Undo/redo cannot create material or duplicate a module.
- Compiled mass/COM/inertia matches independent analytical fixtures.
- Splits preserve every persistent part ID and total mass/momentum within defined tolerances.
- Invalid sockets, shapes, scales, and capacities produce a clear rejected edit.
- A broad stable hull behaves differently from a narrow hull under the same load.
- Off-center cargo changes trim/roll in the expected direction.
- Repeated topology changes do not leave stale handles or contact references.
- A checkpoint is internally consistent across simulation, topology, cargo, and inventory.
- The complete visible expedition survives save/reload, reset, and later reconnect.
- Dynamic submerged geometry, shadows, and temporal disocclusion pass image tests.

Preserve the existing LEGO terrain/contact, asset parity, shader, browser-control, and startup gates. Extend them where the new behavior changes the contract. Do not treat a skipped GPU test as positive hardware evidence.

## 12. Decisions to hold until evidence arrives

| Question | Default for now | Evidence that changes it |
|---|---|---|
| What is the game? | Build, explore, salvage | Human expedition tests |
| Arbitrary builder on day one? | Editable starter hull first | Players need freedom beyond the socket proof |
| GPU or CPU machine solver? | GPU skiff proof; explicit articulation comparison | Stability, latency, development effort, platform/hosting cost |
| Fully destructible terrain? | Authored editable wrecks over heightfield | Core missions require excavation |
| Solo or multiplayer first? | Complete solo loop; two-player gate later | A proven co-op benefit worth its engineering cost |
| Browser as full shipping target? | Desktop demo plus parity target | Content memory, loading, input, and session evidence |
| Vehicles beyond boats? | Amphibious and cranes after the first loop | Missions justify suspension/traction/articulation work |
| How big is production? | Cost after slice and content throughput | Staff, budget, player evidence, measured asset rates |

The immediate success condition is concrete: **a player builds a craft, encounters a problem caused by its design, recovers the cargo through a meaningful adjustment, and wants to go out again.**

## 13. Source map and evidence limits

Line references below are tied to the review baseline. Code recommendations are proposals unless explicitly described as current. Existing benchmark reports are retained evidence from earlier runs, not fresh performance claims.

| ID | Evidence |
|---|---|
| E1 | [README](/home/modkin/workspace/schneiderlo/voxys/README.md:1), [existing WRECKWATER contract](/home/modkin/workspace/schneiderlo/voxys/docs/wreckwater_dead_haul.md:1), [actual browser default](/home/modkin/workspace/schneiderlo/voxys/web/index.html:229) |
| E2 | [LEGO implementation and limits](/home/modkin/workspace/schneiderlo/voxys/docs/lego-shore.md:1), [CPU surface](/home/modkin/workspace/schneiderlo/voxys/src/terrain/lego_surface.hpp:17), [layout cache](/home/modkin/workspace/schneiderlo/voxys/src/terrain/lego_layout_cache.hpp:10), [application cache updates](/home/modkin/workspace/schneiderlo/voxys/src/app/application.cpp:4238) |
| E3 | [application frame composition](/home/modkin/workspace/schneiderlo/voxys/src/app/application.cpp:1863), [dynamic primitive draw](/home/modkin/workspace/schneiderlo/voxys/src/app/application.cpp:2028), [terrain cache](/home/modkin/workspace/schneiderlo/voxys/src/render/raycast_path.cpp:1096) |
| E4 | [water depth output](/home/modkin/workspace/schneiderlo/voxys/shaders/water_clipmap.wgsl:731), [primitive depth rejection](/home/modkin/workspace/schneiderlo/voxys/shaders/physics_primitives_compact.wgsl:266), [resident brick rendering](/home/modkin/workspace/schneiderlo/voxys/src/render/primitive_path.cpp:1069) |
| E5 | [physics backend contract](/home/modkin/workspace/schneiderlo/voxys/src/physics/physics_backend.hpp:27), [primitive shapes, rope, brick tag, spawn data](/home/modkin/workspace/schneiderlo/voxys/src/physics/physics_types.hpp:384), [Jolt adapter](/home/modkin/workspace/schneiderlo/voxys/src/physics/jolt/jolt_backend.hpp:59) |
| E6 | [structural graph](/home/modkin/workspace/schneiderlo/voxys/src/gameplay/structural_assembly.hpp:18), [component reconstruction](/home/modkin/workspace/schneiderlo/voxys/src/gameplay/structural_assembly.cpp:150), [fixture limits](/home/modkin/workspace/schneiderlo/voxys/src/gameplay/gameplay_types.hpp:18), [authored skiff damage](/home/modkin/workspace/schneiderlo/voxys/src/game/wreckwater_vessel_damage.hpp:12) |
| E7 | [CPU ocean approximation](/home/modkin/workspace/schneiderlo/voxys/src/render/water_simulation.cpp:25), [mode selection](/home/modkin/workspace/schneiderlo/voxys/src/render/water_simulation.cpp:663), [GPU FFT](/home/modkin/workspace/schneiderlo/voxys/shaders/water_fft.wgsl:147), [water clipmap geometry](/home/modkin/workspace/schneiderlo/voxys/src/render/water_clipmap_mesh.cpp:8) |
| E8 | [native wiring and WASM rejection](/home/modkin/workspace/schneiderlo/voxys/src/app/application.cpp:683), [GPU capability flags](/home/modkin/workspace/schneiderlo/voxys/src/physics/gpu/gpu_physics_backend.cpp:4292), [authority scope](/home/modkin/workspace/schneiderlo/voxys/src/server/wreckwater_authority_runtime.hpp:22), [authority readback](/home/modkin/workspace/schneiderlo/voxys/src/server/wreckwater_authority_runtime.cpp:2951), [replay contract](/home/modkin/workspace/schneiderlo/voxys/src/game/wreckwater_replay.hpp:122) |
| E9 | [VMESH format](/home/modkin/workspace/schneiderlo/voxys/src/moto/vmesh.hpp:1), [offline importer](/home/modkin/workspace/schneiderlo/voxys/tools/gltf_vmesh_tool.cpp:1), [mesh renderer limits](/home/modkin/workspace/schneiderlo/voxys/src/render/mesh_path.hpp:31) |
| E10 | [LEGO hardware/validation record](/home/modkin/workspace/schneiderlo/voxys/docs/benchmarks/lego-2026-09-07/README.md:1), [stored successful challenge capture](/home/modkin/workspace/schneiderlo/voxys/docs/benchmarks/lego-2026-09-07/final-1080/playground/success.png), [stored shore capture](/home/modkin/workspace/schneiderlo/voxys/docs/benchmarks/lego-2026-09-07/final-1080/world/shore-overview.png) |
| E11 | [physics benchmark scope](/home/modkin/workspace/schneiderlo/voxys/docs/physics-baseline.md:326), [human evidence limits](/home/modkin/workspace/schneiderlo/voxys/docs/product-vertical-slices.md:1) |
| E12 | [oversized-body broad phase](/home/modkin/workspace/schneiderlo/voxys/shaders/physics_broad_phase.wgsl:714) |
| E13 | [WASM queue bound](/home/modkin/workspace/schneiderlo/voxys/src/engine/platform/wasm/entry.cpp:75), [build/deploy startup gate](/home/modkin/workspace/schneiderlo/voxys/.github/workflows/pages.yml:58) |
| E14 | [playground state and caps](/home/modkin/workspace/schneiderlo/voxys/src/game/lego_playground.hpp:14), [placement and challenge](/home/modkin/workspace/schneiderlo/voxys/src/game/lego_playground.cpp:92), [stacking expectations](/home/modkin/workspace/schneiderlo/voxys/tests/test_lego_playground.cpp:81), [browser HUD/audio](/home/modkin/workspace/schneiderlo/voxys/web/lego_playground.js:1) |

External technical references were checked against the primary sources linked where used. The generated visual and its prompt are stored beside this blueprint. No gameplay or engine implementation was changed by this planning work.
