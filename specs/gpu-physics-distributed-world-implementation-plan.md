# Voxys GPU Physics and Distributed World Implementation Plan

> **Status:** Proposed implementation source of truth  
> **Audience:** implementation agents, engine developers, rendering developers, networking developers, benchmark authors, and reviewers  
> **Voxys baseline reviewed:** `0cd067e0f8fe3bd3507056505737f9e21c4d01d4` (`main`, 2026-07-13)  
> **Box3D reference reviewed:** `d421e45c828f6f853a145f726f0b9425d31146eb` (`erincatto/box3d`, 2026-07-13)  
> **Primary targets:** native WebGPU and browser WebGPU  
> **Future server target:** CUDA or another native GPU compute backend using the same simulation contract  
> **Document purpose:** this file is deliberately self-contained. An implementation agent must not need access to the conversation that produced it.
>
> **Current implementation status:** see [GPU Physics: Remaining Work](../docs/gpu-physics-remaining-work.md). Checked phase tasks do not replace the outstanding integration and exit-gate validation listed there.

---

## 1. How to use this document

This document defines the intended architecture and the staged implementation plan for replacing the current Jolt-centered physics path with a GPU-native physics system tailored to Voxys, while retaining a CPU reference backend and preparing for server-authoritative multiplayer and distributed worlds.

The words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are used normatively:

- **MUST / MUST NOT**: required for correctness, determinism, compatibility, or architectural integrity.
- **SHOULD / SHOULD NOT**: strong default; deviations require an explicit decision-record entry and benchmark or correctness evidence.
- **MAY**: optional or deferred.

The implementation must be incremental. Jolt is not removed first. The expected progression is:

1. measure and preserve the existing behavior;
2. introduce a backend abstraction;
3. add a pinned Box3D CPU reference backend;
4. build a GPU-resident static-contact path;
5. add deterministic GPU data-parallel primitives;
6. add dynamic broad phase, narrow phase, persistent manifolds, graph coloring, and a Box3D-inspired Soft Step solver;
7. move rendering to consume physics buffers directly;
8. add sleeping, CCD, deterministic replay, and a strict lockstep mode;
9. add server-authoritative networking and island-level rollback;
10. add a native server GPU backend and retire Jolt only after all gates pass.

This is not a mandate to implement every advanced feature immediately. It is a map that preserves the long-term architecture while identifying practical vertical slices.

---

## 2. Executive summary

Voxys currently has a cross-platform Jolt integration behind `src/physics/physics_world.hpp`. The implementation uses `JPH::JobSystemSingleThreaded`, sets a fixed maximum of 16,384 bodies, streams nearby 256-sample terrain tiles into Jolt, applies water buoyancy on the CPU, and copies every body transform into a CPU `DynamicBodySnapshot` vector for rendering. The renderer then performs CPU culling, converts poses into 80-byte model-matrix instances, and uploads them to a WebGPU storage buffer.

That path is useful as a baseline but fundamentally limits the intended scale. The target architecture is a GPU-native rigid-body backend in which:

- body state remains resident on the GPU;
- terrain, water, broad phase, contact generation, constraint solving, sleeping, culling, and indirect rendering are GPU stages;
- the normal frame path performs no full-body CPU readback and no CPU-generated model-matrix upload;
- the solver follows Box3D's 3D Soft Step structure rather than a simplistic particle or pure Jacobi solver;
- four-point manifolds, persistent warm-start impulses, central two-dimensional friction, twist friction, rolling resistance, gyroscopic correction, speculative contacts, and graph-colored in-place solving are preserved where they improve visible quality;
- heightfield contacts are specialized for Voxys and solved body-locally without consuming dynamic graph colors;
- dynamic contacts use deterministic graph coloring, with a deterministic gather-based overflow path;
- a Box3D CPU backend acts as the correctness oracle and production fallback during development;
- deterministic floating-point execution and strict cross-vendor lockstep are separate, honestly specified modes;
- the player character remains a latency-critical geometric capsule mover on the CPU until asynchronous GPU querying is proven practical;
- a future authoritative server batches many physics islands and worlds on a server GPU, while browser clients predict, render, generate static chunks, and perform non-authoritative visual work.

The first major GPU acceptance target is:

> **100,000 active simple bodies at a fixed 60 Hz, using four internal Soft Step substeps, with heightfield collision, water interaction, body-body contacts, GPU-resident rendering, and no normal-frame CPU state readback on a representative discrete desktop GPU.**

This is a target, not a current performance claim. Dense contact count and island connectivity matter more than raw body count.

---

## 3. Current Voxys baseline

### 3.1 Existing strengths

The repository already contains important foundations:

- native and browser WebGPU targets;
- Bazel as the recommended development build and CMake parity;
- an `R16Uint` heightmap and max-height mip chain;
- a triangle terrain path and hierarchical compute raycast path;
- GPU water simulation and water rendering;
- primitive rendering for sphere, cube, box, capsule, and cylinder;
- CPU frustum culling and partial instance-buffer updates;
- a Jolt physics facade hidden behind a PIMPL boundary;
- a Jolt virtual character controller;
- full-resolution streamed terrain collision;
- physics tests for terrain, character motion, all five primitive shapes, 10,000 bodies, water behavior, and stable dry-body behavior;
- performance benchmarks for 10,000 and 16,384 body snapshots and water overhead;
- debug and benchmark infrastructure;
- a heightmap import and compression toolchain.

These features should be reused rather than rewritten indiscriminately.

### 3.2 Existing physics behavior

The present `PhysicsWorld` API exposes:

- initialization and shutdown;
- heightmap attachment;
- a flat water plane plus a CPU water-surface callback;
- character creation and movement;
- five throwable shapes;
- per-frame physics update;
- a CPU vector of body snapshots for rendering.

The current Jolt configuration uses:

- `JPH::JobSystemThreadPool` on native builds, with automatic worker count;
- `JPH::JobSystemSingleThreaded` in the WASM build without pthreads;
- `kMaxBodies = 16384`;
- `kMaxBodyPairs = 65536`;
- `kMaxContactConstraints = 16384`;
- a maximum substep of `1 / 60 s`;
- streamed heightfield tiles containing 256 samples and 255 cells;
- a three-by-three tile window around the character or newly thrown object;
- per-shape friction, restitution, and buoyancy constants.

The 16,384 body limit is a Voxys configuration value, not an intrinsic Jolt
limit. GPU speedup claims must retain both multithreaded and single-threaded
Jolt measurements so scheduler scaling remains visible.

### 3.3 Existing terrain convention

The canonical current height mapping is:

```text
normalized = sample / 65535
worldY = heightScale * (2 * normalized - 1)
```

The terrain is centered around the origin in X and Z. A sample value near 32768 therefore maps close to world Y zero.

The current triangle renderer uses a fixed diagonal from the quad's top-left vertex to its bottom-right vertex:

```text
triangle 0: top-left, bottom-right, bottom-left
triangle 1: top-left, top-right, bottom-right
```

This differs from the diagonal used by the reviewed Box3D heightfield implementation. The GPU physics implementation MUST preserve the Voxys renderer's canonical topology unless all render, raycast, collision, import, and test paths are changed together. Do not copy Box3D's heightfield triangle split blindly.

### 3.4 Existing render-transfer bottleneck

The present frame path is approximately:

```text
Jolt simulation
    -> lock and copy all body poses into CPU snapshots
    -> CPU frustum culling
    -> CPU grouping by shape
    -> CPU quaternion/translation/scale to 4x4 matrix conversion
    -> upload 80-byte model/color instance records
    -> draw primitives
```

An 80-byte instance record means:

| Active bodies | Full instance upload per frame | At 60 frames/s |
|---:|---:|---:|
| 16,384 | 1.25 MiB | 75 MiB/s |
| 100,000 | 8.0 MB | 480 MB/s |
| 1,000,000 | 80 MB | 4.8 GB/s |

These figures exclude snapshot construction, body locking, culling, grouping, matrix construction, and command overhead. The long-term render path must therefore read compact pose and shape data directly from physics buffers.

---

## 4. Product and engine design target

The engine is intended to enable browser-first, physics-driven games with:

- block-built or primitive-built machines and structures;
- large numbers of whole rigid pieces;
- heightmap terrain and ocean environments;
- understandable structural breakup at authored or graph-defined connections;
- co-op and competitive interaction;
- deterministic replay and server-authoritative outcomes;
- procedural worlds generated from seeds;
- different client render and prediction budgets without different authoritative truth.

The current renderer is not an AAA asset renderer. Near-term vertical slices SHOULD use:

- spheres, boxes, capsules, cylinders, and simple fixed hulls;
- block-built trains, rafts, cranes, towers, forts, vehicles, and machines;
- simple solid colors or lightweight textures;
- capsule or block avatars;
- whole-section breakage rather than arbitrary high-resolution fracture;
- scale, fog, lighting, water, motion, and collapse as the primary spectacle.

Three representative product directions are useful for prioritization:

1. **Demolition League**: the fastest engine-validation product; deterministic collapse, scoring, replay, and browser sharing.
2. **Deadweight**: a practical first substantial co-op game; transport one enormous physical object with cranes, winches, carriers, and environmental hazards.
3. **Wreckwater**: the long-term flagship; modular ships, meteor salvage, buoyancy, flooding, structural breakup, authoritative multiplayer, and procedural archipelagos.

The physics architecture must support these without requiring a general-purpose AAA engine before the first game is fun.

---

## 5. Goals

### 5.1 Core simulation goals

The implementation MUST aim to provide:

- a GPU-resident rigid-body world for native and browser WebGPU;
- sphere, cube/box, capsule, and cylinder support;
- heightfield terrain collision using the same source samples as rendering;
- body-body collision with persistent manifolds;
- Box3D-inspired Soft Step integration and solving;
- static contact specialization;
- sleeping and wake propagation;
- speculative contacts and selected CCD;
- water buoyancy and drag;
- stable body handles and command-based mutation;
- direct GPU rendering from body state;
- explicit capacity management and overflow diagnostics;
- a CPU reference backend and a legacy Jolt backend during migration.

### 5.2 Determinism goals

The implementation MUST provide two separate contracts:

- **DeterministicFloat**: deterministic ordering and repeatability on a certified adapter/backend/build combination; high throughput; no claim of universal cross-vendor bit identity.
- **Lockstep**: explicitly defined fixed-point or integer state and operations; bit-identical authoritative hashes across certified WebGPU and native server backends.

Both modes MUST use canonical ordering and MUST NOT depend on race-ordered appends, unspecified floating-point atomics, subgroup size, or invocation scheduling.

### 5.3 Rendering goals

The final renderer MUST:

- consume GPU body IDs and physics buffers directly;
- compute model transforms in the vertex shader;
- perform GPU frustum culling and shape compaction;
- generate indirect draw arguments on the GPU;
- avoid full-body CPU snapshot generation during normal rendering;
- retain an optional asynchronous debug snapshot path.

### 5.4 Multiplayer goals

The long-term networking architecture MUST:

- remain server authoritative for consequential state;
- allow client prediction and island-local rollback;
- distinguish static generation chunks, interest cells, and dynamic physics islands;
- ensure one authoritative worker owns every connected dynamic island for a tick;
- migrate islands before cross-worker contact;
- replicate inputs and causes rather than accepting client transforms;
- support WebTransport with WebRTC DataChannel fallback;
- permit client-assisted visual or low-consequence work without trusting it for damage, ownership, inventory, or persistence.

---

## 6. Non-goals and scope limits

The first GPU backend MUST NOT attempt to implement all of Jolt or all of Box3D.

The following are deferred unless a milestone explicitly adds them:

- arbitrary dynamic triangle meshes;
- arbitrary high-vertex convex hulls in the hot path;
- compound dynamic bodies with unlimited shapes;
- a complete joint library;
- vehicles with dedicated suspension and tire models;
- ragdolls;
- soft bodies, cloth, or deformable solids;
- full volumetric fluid simulation;
- arbitrary per-voxel rigid-body simulation for large vehicles;
- one million bodies in one dense, highly connected pile;
- client authority over damage or persistent physics outcomes;
- seamless thousand-player MMO behavior in the first networking milestone;
- unrestricted user-authored scripting inside authoritative physics;
- automatic replacement of every CPU query with a synchronous GPU query.

A million resident bodies is a reasonable eventual goal when most are sleeping or inactive. A million mutually contacting bodies is a different problem and is not an acceptance target.

---

## 7. Reference architecture and inspirations

### 7.1 Box3D

The reviewed reference is:

- repository: `https://github.com/erincatto/box3d`
- pinned reviewed commit: `d421e45c828f6f853a145f726f0b9425d31146eb`
- license: MIT

Relevant Box3D design features:

- portable C17 and data-oriented handles;
- extensive CPU multithreading and SIMD;
- graph-colored constraint solving with 24 colors and overflow;
- dynamic-static contact priority;
- a Soft Step solver with prepare, integrate velocity, warm start, biased solve, integrate position, relaxation, restitution, and impulse storage stages;
- four-point 3D manifolds;
- central two-dimensional friction, twist friction, and rolling resistance;
- weighted friction centers for temporal continuity;
- one fixed Newton iteration for gyroscopic torque correction;
- speculative contacts and CCD;
- island-based sleeping;
- a geometric capsule character mover outside rigid-body simulation;
- cross-platform CPU determinism, recording, replay, and state hashes;
- a large-world precision boundary in which absolute position may be double while hot local math remains float.

Voxys SHOULD reuse algorithms and ideas, not blindly port CPU scheduling structures. Any source-derived implementation MUST preserve Box3D's MIT attribution.

### 7.2 GPU Gems 3, Chapter 29

Reference:

`https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-29-real-time-rigid-body-simulation-gpus`

Useful principles:

- keep simulation data resident on the GPU;
- organize simulation as explicit GPU stages;
- use a spatial grid to restrict collision candidates;
- update position and quaternion on the GPU;
- render directly from GPU pose data;
- recognize that a dense world-sized uniform grid wastes memory in sparse worlds.

Voxys should adopt GPU residency and spatial bucketing, but not GPU Gems' particle-cluster rigid-body representation as the main shape model. Voxys has a small, known analytic shape set and can generate higher-quality contacts with much less data.

### 7.3 Dennis Gustafsson, “Parallelizing the physics solver”

Reference:

`https://www.youtube.com/watch?v=Kvsvd67XUKw`

The relevant lesson is that integration and broad phase are not the hardest parallel problem. Conflicting writes during constraint solving are the central issue. Voxys therefore uses conflict-free graph colors for the common path and a deterministic gather-based fallback for overflow.

### 7.4 WebGPU and WGSL constraints

References:

- `https://www.w3.org/TR/webgpu/`
- `https://www.w3.org/TR/WGSL/`

Important constraints:

- core WGSL atomic types are `atomic<i32>` and `atomic<u32>`;
- portable floating-point atomic accumulation cannot be assumed;
- floating-point reassociation and fusion rules prevent a universal CPU-style exact-float contract;
- subgroup availability and size vary and cannot define simulation semantics;
- storage-buffer, dispatch, binding, and workgroup limits vary by adapter.

These constraints are why the common solver avoids body-state atomics and why strict lockstep requires an explicit integer arithmetic contract.

### 7.5 Networking references

- WebTransport: `https://www.w3.org/TR/webtransport/`
- WebRTC Data Channels: `https://datatracker.ietf.org/doc/html/rfc8831`

WebTransport provides unreliable datagrams plus reliable streams in one secure session. WebRTC Data Channels use SCTP over DTLS over ICE/UDP and can provide reliable, partially reliable, ordered, or unordered delivery.

---

## 8. Non-negotiable architectural decisions

1. `PhysicsWorld` becomes a facade over multiple backends.
2. Jolt remains available until CPU reference, GPU correctness, rendering, WASM, and benchmark gates pass.
3. A pinned Box3D backend is implemented before the full GPU solver and serves as the main CPU oracle.
4. The fixed public tick begins at 60 Hz with four internal substeps.
5. The primary dynamic solver is graph-colored, in-place Soft Step/Gauss-Seidel, not global Jacobi.
6. Static heightfield contacts are solved body-locally and do not consume dynamic graph colors.
7. Jacobi/gather is reserved for graph-color overflow and selected massively parallel fallback work.
8. Semantic arrays are produced by count, scan, and stable scatter; race-ordered append is forbidden.
9. Pair keys, contact keys, event keys, and command keys use canonical stable ordering.
10. Body state remains on the GPU during normal operation.
11. Rendering reads body pose, orientation, dimensions, and shape directly from physics buffers.
12. The CPU character is a geometric mover, not a general dynamic rigid body.
13. Authoritative physical water is separate from richer visual water when lockstep is required.
14. All capacities are explicit, queryable, benchmarked, and diagnosed.
15. Buffer overflow is deterministic and visible; it is never silently dependent on which invocation won a race.
16. Adapter-specific optimization may change work partitioning but not semantic ordering or arithmetic contracts.
17. Static world chunks, network interest cells, and physics islands remain separate concepts.
18. Every connected dynamic island has one authoritative server worker for a tick.
19. Clients send inputs and commands, not authoritative transforms or hit outcomes.
20. Large voxel-built machines are normally aggregate rigid bodies with a structural connectivity graph, not thousands of internally colliding bodies.

---

## 9. High-level runtime architecture

```text
CPU application/gameplay
  - input sampling
  - fixed-tick accumulation
  - canonical command stream
  - CPU character mover
  - occasional async queries/readbacks
  - networking and rollback history
          |
          v
GPU physics command encoding
  1. apply commands
  2. compact awake bodies
  3. integrate external forces and water
  4. generate terrain contacts
  5. build dynamic sparse grid
  6. stable-sort grid entries
  7. construct occupied-cell ranges
  8. count and emit canonical candidate pairs
  9. stable-sort and unique pairs
 10. merge with persistent contacts
 11. bucket shape pairs
 12. run narrow-phase kernels
 13. match/reduce persistent manifolds
 14. build islands and/or color dynamic constraints
 15. prepare Soft Step constraints
 16. execute substeps
 17. restitution and impulse storage
 18. sleep/wake updates
 19. CCD correction for selected fast bodies
 20. state/event hashes
 21. GPU culling and indirect draw preparation
          |
          v
GPU renderer
  - terrain and water
  - primitive/assembly geometry
  - body-ID-driven vertex transforms
  - indirect draws
```

The physics pipeline should usually be encoded into the same frame command encoder before body rendering. The server version may encode simulation without rendering.

---

## 10. Backend abstraction

### 10.1 Proposed backend types

```cpp
namespace voxy::physics {

enum class BackendType : uint8_t {
    JoltLegacy,
    Box3DReference,
    WebGpuSoft,
};

struct BackendCapabilities {
    bool gpuResidentState = false;
    bool directRenderView = false;
    bool synchronousCharacter = false;
    bool deterministicFloat = false;
    bool lockstep = false;
    bool bodyBodyContacts = false;
    bool continuousCollision = false;
};

struct PhysicsInitContext {
    BackendType requestedBackend = BackendType::JoltLegacy;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    uint32_t maxBodies = 16'384;
    uint32_t maxActiveBodies = 16'384;
    uint32_t maxPairs = 65'536;
    uint32_t maxContacts = 65'536;
    uint32_t maxManifolds = 65'536;
    bool enableValidation = false;
};

} // namespace voxy::physics
```

WebGPU types may be hidden behind a forward-declared GPU context wrapper if including WebGPU headers in the public physics facade is undesirable.

### 10.2 Facade and backend interface

The existing `PhysicsWorld` name may remain as the public facade. The implementation should move backend-specific behavior behind an internal interface.

```cpp
class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;

    virtual bool initialize(const PhysicsInitContext&) = 0;
    virtual void shutdown() = 0;
    virtual BackendCapabilities capabilities() const noexcept = 0;

    virtual bool setTerrain(const TerrainDesc&) = 0;
    virtual void clearTerrain() = 0;

    virtual void enqueue(std::span<const PhysicsCommand>) = 0;

    // CPU backends execute here. GPU backends may only update scheduling state.
    virtual void stepCpu(const FixedStep&) = 0;

    // GPU backends encode compute passes here. CPU backends no-op.
    virtual void encodeGpuStep(WGPUCommandEncoder, const FixedStep&) = 0;

    virtual CharacterMoverResult moveCharacter(const CharacterMoveRequest&) = 0;

    virtual PhysicsRenderView renderView() const = 0;
    virtual void requestDebugSnapshot(DebugSnapshotRequest) = 0;
    virtual std::optional<DebugSnapshot> pollDebugSnapshot() = 0;

    virtual PhysicsStats stats() const = 0;
};
```

The exact API may change, but the separation between CPU execution, GPU command encoding, synchronous character movement, direct rendering resources, and asynchronous debug snapshots MUST remain.

### 10.3 Stable handles

Replace the implicit “vector index forever” model with generation-checked handles:

```cpp
struct BodyHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct ShapeHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};
```

Index zero may remain invalid. Destruction increments generation. Free slots are reused in ascending index order at tick boundaries to preserve determinism.

### 10.4 Command-based mutation

All world mutation is converted into commands:

```cpp
enum class PhysicsCommandType : uint16_t {
    SpawnBody,
    DestroyBody,
    ApplyImpulse,
    ApplyForce,
    SetVelocity,
    SetAngularVelocity,
    Teleport,
    SetMaterial,
    Wake,
    Sleep,
    SetKinematicTarget,
};

struct PhysicsCommandHeader {
    uint64_t tick;
    uint64_t sequence;
    PhysicsCommandType type;
    uint16_t payloadBytes;
};
```

Commands are sorted by `(tick, command priority, sequence)` before application. A server-assigned sequence becomes authoritative in networked worlds.

The current `throwBody` helper may remain as a convenience wrapper that creates a `SpawnBody` command.

---

## 11. Time stepping

### 11.1 Fixed tick

The simulation MUST use a fixed public tick. Initial values:

```text
public tick: 1 / 60 second
internal substeps: 4
substep duration: 1 / 240 second
```

The application accumulates render-frame time and executes zero or more complete physics ticks. It MUST NOT feed arbitrary render delta time into the authoritative solver.

A maximum catch-up count protects the browser from a spiral of death. When exceeded, the application records a stall and either slows presentation or requests a resync; it does not alter the authoritative timestep.

### 11.2 Tick identity

Every step has a monotonically increasing `uint64_t tick`. Tick identity is used by:

- commands;
- replay;
- state hashes;
- network packets;
- rollback history;
- island migration;
- deterministic water phases;
- spawn/destruction ordering.

### 11.3 Rendering interpolation

Rendering MAY interpolate between confirmed poses or extrapolate predicted local poses. Interpolation state is presentation-only and MUST NOT feed back into physics.

---

## 12. Numerical modes and large-world representation

### 12.1 DeterministicFloat

This is the first GPU implementation mode.

Properties:

- `f32` body state and solver math;
- stable command, pair, contact, manifold, color, and event ordering;
- fixed substep and iteration counts;
- no body-state atomics;
- no semantic dependence on subgroup behavior;
- repeatable hashes on a certified adapter/backend/build;
- not advertised as universal cross-vendor bit identity.

This mode should provide the highest throughput and is suitable for ordinary server-authoritative client prediction.

### 12.2 Lockstep

This mode is added only after the float architecture is correct and benchmarked.

Proposed initial formats:

| Quantity | Initial representation | Notes |
|---|---|---|
| Sector coordinate | signed `i32` | 256 m sectors are a reasonable initial choice |
| Local position | Q20.12 signed fixed point | approximately 0.244 mm resolution |
| Linear velocity | Q16.16 | wide practical game range |
| Angular velocity | Q12.20 | sufficient rotational resolution |
| Quaternion | Q2.30 | normalized by fixed-iteration integer routine |
| Contact normal/tangent | Q1.30 | unit vectors |
| Inverse mass/inertia | Q16.16 or tuned per range | explicit saturation |
| Friction/restitution | Q4.28 | non-negative material values |
| Time | integer tick/substep | never accumulated float time |
| Sleep time | integer tick count | no float threshold time |

The exact bit allocation MUST be validated against range, precision, overflow, and performance tests before being frozen.

WGSL does not guarantee native 64-bit integer support. Operations requiring a 64-bit intermediate must use a portable two-word representation or a proven bounded 32-bit formulation. Rounding, saturation, division, reciprocal square root, and normalization rules must be explicit and shared by CPU, WGSL, and native server implementations.

### 12.3 World sectors

Portable WebGPU cannot rely on `f64`. Large worlds use:

```text
absolute world position = integer sector + local coordinate
```

Hot contact and solver math occurs in a nearby sector or island-relative frame. Rendering subtracts the camera sector before converting to ordinary `f32`.

No broad-phase key may depend on lossy conversion of a far-world absolute position to float.

---

## 13. Materials, bodies, and shapes

### 13.1 Initial body model

Initial GPU scope:

- static terrain;
- dynamic bodies;
- optional kinematic bodies after dynamic parity;
- one collision shape per dynamic body;
- no joints in the first solver milestone.

One-shape bodies cover the current throwable sandbox and most early game prototypes. Multiple shapes and joints can be added after the core path is stable.

### 13.2 Initial shapes

```cpp
enum class ShapeType : uint8_t {
    Sphere,
    Box,
    Capsule,
    Cylinder,
};
```

`Cube` is a box with equal half extents. The public compatibility enum may retain `Cube` and `Box`, but the collision backend should share the box implementation.

### 13.3 Material model

```cpp
struct PhysicsMaterial {
    float friction = 0.65f;
    float restitution = 0.25f;
    float rollingResistance = 0.0f;
    float density = 1.0f;
    uint32_t flags = 0;
};
```

Material combination functions MUST be pure and deterministic. Custom host callbacks that alter authoritative results are deferred because they complicate replay and server validation.

### 13.4 GPU body storage

Start with split, aligned storage buffers rather than one oversized struct:

```wgsl
struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyMotion {
    linearVelocity_sleep : vec4<f32>,
    angularVelocity_flags : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
};

struct BodyWorldMeta {
    sector_generation_flags : vec4<i32>,
};
```

Metadata packed into float lanes must use `bitcast` and documented masks. If this proves error-prone, use dedicated `u32` buffers.

A starting memory budget of roughly 96–128 bytes of core resident state per body is acceptable. One million resident bodies then require roughly 96–128 MB before broad-phase and contact scratch. Exact sizes must be reported by the debug overlay.

### 13.5 Awake and sleeping storage

Awake bodies are compacted into a dense active list. Sleeping bodies remain resident but are excluded from most per-tick passes and live in a persistent sleeping broad-phase structure.

Moving bodies between awake and sleeping sets MUST preserve stable body handles.

---

## 14. GPU buffer inventory

The GPU backend should own explicit buffers for:

### Persistent buffers

- body poses;
- body motion;
- body properties and shape data;
- body metadata and generations;
- active/sleeping state;
- free body IDs;
- persistent contacts and manifolds;
- previous graph colors;
- sleeping-grid entries and ranges;
- material table;
- authoritative physical-water parameters;
- state hashes;
- indirect rendering geometry tables.

### Per-tick or reusable scratch

- command upload and decoded command records;
- active body IDs;
- predicates and scan scratch;
- terrain contact counts and contacts;
- dynamic grid entries;
- radix-sort ping-pong buffers and histograms;
- occupied-cell ranges;
- candidate pair counts and pairs;
- sorted/unique pairs;
- contact merge output;
- shape-pair buckets;
- narrow-phase temporary results;
- color claims and color ranges;
- overflow endpoint deltas and body adjacency;
- island union-find data;
- event records;
- visible body IDs;
- per-shape visible ranges;
- indirect draw arguments;
- telemetry counters;
- asynchronous readback ring buffers.

Scratch allocation SHOULD use a reusable arena with fixed offsets computed from configured capacities. Do not allocate and destroy large WebGPU buffers each frame.

---

## 15. Deterministic data-parallel primitives

Several primitives are foundational and must be implemented and tested before the full solver.

### 15.1 Stable compaction

Semantic compaction uses:

```text
predicate
  -> exclusive scan
  -> stable scatter
```

This applies to active bodies, grid entries, candidate pairs, contacts, events, and visible instances.

### 15.2 Prefix scan

Provide portable scan kernels for `u32` and, where needed, pairs of `u32`. The semantic path must have a prescribed reduction topology. Adapter-specific subgroup acceleration MAY be used only when it produces identical integer output and has a portable fallback.

### 15.3 Stable radix sort

Required sorts include:

- grid entries by cell key then body ID;
- candidate pairs by pair key;
- contacts by pair key;
- body adjacency by body ID then constraint key;
- events by event key;
- visible IDs by shape if count/scatter does not already produce buckets.

Start with an 8-bit digit stable radix sort. A 64-bit logical key is represented as two `u32` words and sorted lexicographically. Workgroup size and digit width may be adapter-tuned if output order remains identical.

### 15.4 Adjacent unique and merge

Provide:

- adjacent duplicate removal for sorted pairs;
- deterministic merge of current pair keys with previous persistent contact keys;
- deterministic free-list assignment for newly created contacts.

### 15.5 Prohibited pattern

This pattern MUST NOT determine semantic ordering:

```wgsl
let index = atomicAdd(&count, 1u);
output[index] = value;
```

Atomic counters may gather statistics. Ordered output must use count/scan/scatter or a canonical owner rule.

---

## 16. Terrain collision

### 16.1 Shared source of truth

Rendering, character movement, ray queries, and rigid-body collision MUST share:

- the same `uint16_t` height samples;
- the same world-height formula;
- the same centered XZ origin;
- the same cell scale;
- the same fixed triangle diagonal;
- the same hole/material metadata when added;
- the same edge/adjacency interpretation.

A small shared `TerrainTopology` module should define CPU helpers and generate or mirror WGSL constants.

### 16.2 Terrain topology

For cell corners:

```text
TL ---- TR
|    /  |
|  /    |
BL ---- BR
```

The current renderer uses the TL-BR diagonal:

```text
triangle 0 = TL, BR, BL
triangle 1 = TL, TR, BR
```

Physics feature IDs should encode:

```text
terrain tile/chunk
cell X
cell Z
triangle index
face/edge/vertex feature
```

### 16.3 Candidate generation

For each active body:

1. compute the conservative world AABB, including speculative margin;
2. project its XZ bounds into heightfield cell coordinates;
3. use max-height mips to reject the body if its minimum Y is above all overlapped terrain;
4. count overlapped candidate cells;
5. emit a body-to-cell range in stable order;
6. run a shape-specific terrain narrow phase.

The first implementation may directly iterate overlapped cells in one body invocation if the maximum footprint is small. Large-footprint bodies must enter an oversized path rather than creating unbounded loops.

### 16.4 Contact generation

Generate up to four reduced contact points per body-terrain manifold. Use deterministic tie-breaking by deepest separation, spatial spread, and feature ID.

Internal edge suppression is required to prevent boxes and capsules snagging on triangle diagonals. Initial options:

- compute adjacency/concavity flags at terrain upload time;
- derive smooth edge behavior from shared heightfield neighborhoods;
- port the relevant Box3D heightfield edge concepts while preserving the Voxys diagonal.

### 16.5 Static body-local solve

Heightfield contacts do not require graph coloring. One invocation owns one dynamic body and solves all of its static terrain manifolds in canonical feature order. Static terrain receives no writes.

Benefits:

- no body conflicts;
- no static graph colors;
- terrain contacts are naturally high priority;
- fewer dispatches;
- excellent body-state locality.

This specialization is a key Voxys advantage over a general engine.

---

## 17. Dynamic broad phase

### 17.1 Initial pragmatic design: sparse sorted single-level grid

The first dynamic broad phase should target the current shape sizes. Use a configurable base cell size slightly larger than the maximum common body diameter plus speculative margin.

Each active body emits entries for every overlapped cell. A small body usually emits one to eight entries. A hard per-body entry cap routes oversized bodies to a separate path.

Entry key:

```text
sector or local-region key
cell X/Y/Z encoded with Morton or packed integer coordinates
body ID as secondary order
```

Because portable WGSL cannot assume native `u64`, represent logical 64-bit keys as `{high, low}`.

Pipeline:

```text
count body grid entries
  -> scan
  -> stable scatter entries
  -> radix sort by cell key and body ID
  -> construct occupied-cell ranges
  -> count candidate pairs
  -> scan
  -> stable scatter canonical pairs
  -> radix sort pair keys
  -> adjacent unique
```

### 17.2 Neighbor policy

To avoid symmetric duplicate work, process:

- pairs within the same cell;
- only a fixed forward half of neighboring cells.

Even with this policy, bodies that overlap multiple cells may generate duplicates, so final pair sort/unique remains required.

### 17.3 Oversized bodies and assemblies

Initial oversized fallback options:

- insert into a coarser sparse grid;
- maintain a small LBVH over oversized AABBs;
- test oversized bodies against overlapping occupied cells.

Do not complicate the first common-body path with a fully hierarchical broad phase. Add hierarchy only after measured scenes require it.

### 17.4 Sleeping grid

Sleeping bodies use a persistent sparse grid rebuilt only when a body:

- enters sleep;
- wakes;
- is destroyed;
- is moved kinematically;
- is migrated between world sectors.

Active bodies query both active and sleeping structures. A contact with an active body wakes the sleeping island deterministically.

### 17.5 Capacity overflow

If candidate pair capacity is exceeded:

- set an overflow flag and high-water metric;
- retain pairs by canonical sorted key, not invocation arrival;
- fail validation tests;
- optionally stop the simulation in debug builds;
- never silently accept a race-dependent subset.

Production capacities should be sized so overflow is a configuration fault, not normal behavior.

---

## 18. Persistent contacts

### 18.1 Sorted merge instead of concurrent hash insertion

Maintain previous contacts sorted by canonical pair key. Merge them with current unique pair keys:

```text
current pair only -> new contact
previous contact only -> ended contact
both -> continuing contact
```

Continuing contacts retain:

- contact ID;
- manifold feature IDs;
- normal impulses;
- central friction impulse;
- twist impulse;
- rolling impulse;
- previous graph color;
- sleep/wake and event metadata.

### 18.2 Contact key

```text
pair key = (min(bodyA, bodyB), max(bodyA, bodyB))
```

If multiple shapes per body are added later, extend the key with shape IDs and child indices.

### 18.3 Contact events

Begin/end/hit events are generated into deterministic event arrays and sorted by:

```text
tick, event type priority, body/shape pair key, feature ID
```

User callbacks must run after GPU readback or on the server after canonical event decoding. Callback execution order must not affect the same tick's authoritative physics.

---

## 19. Narrow phase

### 19.1 Shape-pair bucketing

Stable-count and scatter pairs into dedicated kernels:

- sphere-sphere;
- sphere-capsule;
- sphere-box;
- sphere-cylinder;
- capsule-capsule;
- capsule-box;
- capsule-cylinder;
- box-box;
- box-cylinder;
- cylinder-cylinder.

A canonical type ordering avoids duplicate kernels.

### 19.2 Analytic common paths

Implement analytic kernels for:

- sphere-sphere;
- sphere-capsule;
- capsule-capsule;
- sphere-box;
- capsule-box.

These paths should be compact, branch-light, and heavily tested.

### 19.3 Box-box

Use a Box3D-inspired convex manifold approach:

- face separation tests;
- valid edge-pair axis tests using adjacent face normals/Gauss-map reasoning;
- deterministic best-axis selection;
- reference and incident feature selection;
- polygon clipping;
- reduction to at most four points;
- stable feature IDs.

Tie-breaking must use feature indices, not incidental floating-point comparison order alone.

### 19.4 Cylinder

For the first complete-shape milestone, represent the current cylinder as a fixed low-vertex convex hull with authored topology. This gives a common support/manifold path without immediately implementing every analytic finite-cylinder case.

A later optimized cylinder kernel may be added if cylinder-heavy benchmarks show meaningful benefit.

### 19.5 General convex fallback

A bounded GJK-based fallback MAY be implemented for fixed small hulls. EPA should be deferred unless required; SAT/feature clipping for known hulls is preferable for stable manifolds.

No variable unbounded iteration loop is allowed in authoritative simulation. GJK, shape casts, and conservative advancement use fixed iteration limits and deterministic fallback behavior.

### 19.6 Tolerances

Initial constants should be close to Box3D's proven scale, then tuned with tests:

```text
linear slop: about 0.005 m
speculative distance: about 4 * linear slop
contact recycle distance: about 10 * linear slop
maximum manifold points: 4
```

All constants must live in one shared configuration module and have CPU/WGSL parity tests.

---

## 20. Manifold model and friction

### 20.1 Persistent manifold

A manifold contains at most four points:

```cpp
struct ContactPoint {
    LocalAnchor anchorA;
    LocalAnchor anchorB;
    FeatureId featureA;
    FeatureId featureB;
    float baseSeparation;
    float relativeNormalVelocity;
    float normalImpulse;
    float totalNormalImpulse;
};

struct ContactManifold {
    ContactPoint points[4];
    uint32_t pointCount;
    Vec3 normal;
    Vec3 tangent1;
    Vec3 tangent2;
    Vec3 frictionCenterA;
    Vec3 frictionCenterB;
    Vec2 frictionImpulse;
    float twistImpulse;
    Vec3 rollingImpulse;
};
```

Actual GPU layout must be 16-byte aligned and may use structure-of-arrays storage.

### 20.2 Point matching

Match new and old points by:

1. exact feature-ID match;
2. otherwise nearest local anchor within recycle distance;
3. ties broken by lower feature ID;
4. unmatched points start with zero impulse;
5. unmatched old points are discarded.

### 20.3 Manifold reduction

When more than four candidates exist, retain points in deterministic order that maximize:

- deepest penetration or smallest separation;
- contact-area coverage;
- feature stability;
- spatial spread.

### 20.4 Weighted friction center

Preserve Box3D's concept of a weighted central friction anchor. Points near or inside contact receive stronger weight; distant speculative points receive minimal weight. This reduces jitter as points appear and disappear and avoids four independent tangent constraints fighting one another.

### 20.5 Friction components

During the relaxation stage, solve:

- coupled two-dimensional tangential friction using a 2x2 effective mass;
- twist friction around the contact normal;
- optional rolling resistance using angular relative velocity.

Friction limits depend on the accumulated normal impulse.

---

## 21. Constraint graph coloring

### 21.1 Purpose

A graph color contains no two dynamic constraints that write the same dynamic body. Therefore all constraints in one color can update body velocities in place without atomic floating-point operations.

### 21.2 Static contacts

Terrain and other immovable world contacts bypass the dynamic graph and use body-local solving.

### 21.3 Dynamic color count

Start with 32 GPU colors plus overflow. Box3D uses 24 CPU colors; terrain bypass means Voxys should have less static pressure on the color set. Empty colors use zero-work indirect dispatch arguments.

The count is configuration, not an immutable truth. Benchmark overflow rate and dispatch cost before changing it.

### 21.4 Initial coloring implementation

The simplest correct GPU milestone may recolor all active dynamic contacts each tick:

1. sort contacts by pair key;
2. clear owner claims;
3. for each fixed coloring round, each uncolored contact chooses the lowest available color;
4. it submits its sorted contact rank to `owner[body][color]` using integer `atomicMin`;
5. it commits only if it wins at both dynamic endpoints;
6. failed contacts retry next round;
7. remaining contacts enter overflow.

Because the minimum canonical rank wins, invocation scheduling does not change the result.

### 21.5 Persistent color optimization

After correctness:

- continuing contacts first attempt to retain their previous color;
- validate retained colors for conflicts;
- recolor only conflicts and new contacts;
- compact constraints into per-color ranges while preserving pair order.

### 21.6 Claim-table memory

For 100,000 active bodies and 32 colors:

```text
100,000 * 32 * 4 bytes = 12.8 MB
```

This is acceptable for a desktop target and should be compared against alternative representations. Integrated/browser tiers may use fewer colors or a compressed bit/owner scheme.

### 21.7 Dynamic-static priority

If non-terrain static or kinematic constraints eventually enter the graph, reserve high-priority colors or execute them before dynamic-dynamic colors, following Box3D's push-through prevention strategy.

---

## 22. Soft Step solver

### 22.1 Stage sequence

Prepare constraint properties once per public tick, then execute four substeps:

```text
prepare joints/contact constraints

for substep in 0..3:
    integrate velocities
    warm start static body-local contacts
    warm start dynamic colors
    solve biased static normal constraints
    solve biased dynamic colors
    solve overflow with bias
    integrate positions and delta rotations
    relax static constraints without bias
    relax dynamic colors without bias
    relax overflow without bias

apply restitution
store persistent impulses
```

Joints are absent initially, but stage naming should leave room for them.

### 22.2 Velocity integration

For each awake body:

- apply force and gravity;
- apply water force and drag;
- apply Pade-approximated linear and angular damping;
- clamp maximum linear and angular speed;
- update world inverse inertia;
- perform one fixed Newton-Raphson gyroscopic correction in local coordinates for non-spherical inertia.

The one-iteration gyroscopic correction is inspired by Box3D and improves long, thin rotating bodies.

### 22.3 Constraint preparation

Precompute:

- body indices;
- inverse masses;
- world inverse inertia tensors;
- local anchors;
- base separation;
- effective normal mass;
- tangent basis;
- 2x2 tangent mass;
- twist mass;
- rolling mass;
- pre-impact relative normal velocity;
- softness coefficients;
- material coefficients;
- warm-start impulses.

### 22.4 Softness

Use the Box3D-style terms:

```text
biasRate
massScale
impulseScale
```

For fixed timestep and a small material stiffness table, coefficients may be precomputed. Static terrain contacts may use stiffer softness than dynamic-dynamic contacts.

### 22.5 Biased normal solve

For each point:

- compute current separation from delta positions and delta rotations;
- if separation is positive but within speculative range, use speculative velocity bias;
- if penetrating and bias is enabled, apply bounded softness bias;
- compute relative normal velocity;
- compute incremental impulse;
- clamp accumulated normal impulse to non-negative;
- update both body velocities in place.

### 22.6 Relaxation

The relaxation pass disables positional bias. It:

- relaxes normal impulses;
- applies central friction;
- applies twist friction;
- applies rolling resistance.

This separation avoids mixing aggressive penetration correction with friction and improves stack stability.

### 22.7 Restitution

Restitution is a separate post-substep stage based on stored pre-solve relative velocity and accumulated contact impulse. Apply only above a threshold and only to confirmed impulse-producing contacts.

### 22.8 Store impulses

Write normal, friction, twist, and rolling impulses back into persistent manifolds for the next tick.

---

## 23. Small-island workgroup fast path

Many real scenes contain numerous small disconnected islands. A global sequence of color dispatches can spend more time on dispatch and storage-buffer traffic than arithmetic.

For islands below an adapter-tuned threshold:

1. one workgroup owns the island;
2. load body state and constraints into workgroup memory;
3. retain canonical color and pair order;
4. run all four Soft Step substeps with workgroup barriers;
5. write final body state and impulses once.

Requirements:

- same equations and semantic order as the global path;
- deterministic selection of the fast path based only on island size/capacity, not timing;
- differential tests proving equivalent results in the selected determinism mode;
- fallback to global path when workgroup memory or body/constraint count exceeds limits.

This optimization should be added after the global solver is correct.

---

## 24. Overflow solver

A high-degree body may require more colors than configured. A serial GPU overflow path is unacceptable when overflow grows.

Use deterministic Jacobi gather:

### Constraint pass

Each overflow constraint reads previous body velocities and emits separate endpoint deltas:

```text
constraint -> delta for A, delta for B
```

### Body gather pass

Build or reuse a CSR-style adjacency sorted by:

```text
body ID, constraint pair key
```

One invocation per affected body sums deltas in that exact order and applies the result.

Use a small fixed number of overflow iterations. Overflow remains lower priority and may converge more slowly than colored Gauss-Seidel.

Metrics MUST report:

- overflow constraint count;
- maximum body degree;
- overflow iterations;
- time spent in overflow;
- percentage of all contacts in overflow.

A substantial persistent overflow rate is a scene/configuration problem and should trigger investigation.

---

## 25. Islands and sleeping

### 25.1 Island purpose

Graph colors schedule constraint writes. Islands are used for:

- sleeping;
- wake propagation;
- small-island fast-path selection;
- network authority and migration;
- rollback partitioning;
- diagnostics.

### 25.2 Deterministic union-find

Build dynamic contact islands with:

- initial root = body ID;
- each contact proposes the lower body ID as canonical root;
- integer `atomicMin` for order-independent linking;
- fixed pointer-jumping rounds;
- final sort/compaction by root.

Joints later participate in the same connectivity graph.

### 25.3 Sleep criteria

Use integer tick counts even in float mode:

- linear speed below threshold;
- angular speed below threshold;
- contact motion below threshold;
- no recent force, command, or kinematic disturbance;
- all dynamic bodies in the island qualify for N consecutive ticks.

Sleeping stores remain resident and retain persistent contacts where appropriate.

### 25.4 Wake triggers

Wake deterministically on:

- contact with an awake body;
- force or impulse command;
- teleport or kinematic motion;
- contact or joint removal;
- nearby authoritative explosion/event;
- server migration or correction.

---

## 26. Continuous collision detection

CCD is tiered to protect the common path.

### 26.1 Speculative contacts

All ordinary bodies use predicted AABBs and a fixed speculative distance. This handles most normal-speed motion.

### 26.2 Analytic swept paths

Fast spheres and capsules receive specialized swept tests against:

- heightfield triangles;
- spheres;
- capsules;
- boxes.

### 26.3 Bullet path

Explicit bullet bodies use bounded conservative advancement or fixed bisection:

- fixed maximum iterations;
- deterministic tie-breaking;
- capacity-limited bullet list;
- explicit stall and failure counters;
- no variable “iterate until convergence forever” behavior.

### 26.4 Scope

Initial CCD should focus on projectiles and terrain. Full rotational CCD for all shape pairs is deferred until profiling and gameplay require it.

---

## 27. Water physics

### 27.1 Current problem

The present Jolt path snapshots body positions on the CPU, invokes a CPU callback backed by the water simulation, then applies per-body Jolt buoyancy impulses. This does not scale to a GPU-resident world.

### 27.2 GPU integration

Water force is body-local and should be fused into velocity integration.

Each shape has a small fixed set of sample points:

- sphere: center plus optional vertical offsets;
- box: corners or face-center samples;
- capsule: endpoints and center;
- cylinder: cap and radial samples.

For each sample:

- evaluate water height, normal, and flow velocity;
- compute submerged depth/weight;
- apply buoyancy at the sample position;
- apply linear and angular drag;
- clamp pathological forces.

### 27.3 Visual versus physical water

**DeterministicFloat mode:** MAY sample a quantized GPU water field derived from the existing simulation.

**Lockstep mode:** physical water MUST use deterministic tick-derived data, such as a small fixed set of integer harmonic waves or a fixed-point field. The visual FFT water can remain richer and non-authoritative.

Both may share wind direction, nominal wavelength, and phase seed, but visual FFT output must not define authoritative damage or body motion in lockstep.

### 27.4 Water does not connect islands

Water is an external field. It applies body-local forces and does not create dynamic graph connectivity.

---

## 28. Character controller

### 28.1 Rationale

A player camera cannot wait for a full GPU submission and readback every movement tick. The character remains outside the general rigid-body solver.

### 28.2 Geometric capsule mover

Follow the Box3D mover workflow:

```text
compute desired translation
  -> capsule cast
  -> move by safe fraction
  -> gather contact planes
  -> solve planes for overlap correction
  -> clip velocity against blocking planes
```

Implement this as a small deterministic CPU module using:

- the canonical Voxys heightfield formula and diagonal;
- capsule geometry;
- slope, step-up, and step-down rules;
- a bounded number of planes;
- deterministic plane ordering;
- the same sector-relative coordinates as physics.

### 28.3 Nearby dynamic bodies

Options in increasing complexity:

1. character collides only with terrain in the first GPU milestone;
2. maintain a small CPU mirror of nearby gameplay-critical bodies;
3. asynchronously request nearby dynamic broad-phase/query results;
4. promote selected GPU bodies into a CPU authoritative interaction set;
5. submit character impulses to GPU bodies as next-tick commands.

Do not add a synchronous whole-world GPU readback.

---

## 29. Rendering integration

### 29.1 Target render view

Replace snapshot-driven rendering with a lightweight resource view:

```cpp
struct PhysicsRenderView {
    WGPUBuffer poseBuffer;
    WGPUBuffer shapeBuffer;
    WGPUBuffer visibleBodyIds;
    WGPUBuffer perShapeRanges;
    WGPUBuffer indirectDrawArgs;
    uint32_t residentBodyCapacity;
};
```

CPU backends may populate compatible GPU buffers through uploads during the transition.

### 29.2 Vertex shader

The primitive vertex shader receives `instance_index`, maps it to a visible body ID, then reads:

- camera-relative position;
- quaternion;
- dimensions;
- shape/material information.

It rotates and scales the existing primitive geometry directly. No CPU `mat4` is required.

### 29.3 GPU culling

After physics:

1. compute camera-relative bounds;
2. frustum cull;
3. optionally use terrain raycast depth for conservative occlusion;
4. count visible bodies per shape;
5. scan counts;
6. scatter body IDs into per-shape ranges;
7. generate indirect draw arguments.

### 29.4 Compatibility migration

Migration sequence:

1. keep current `PrimitivePath::setInstances` for Jolt;
2. add a second `setPhysicsRenderView` or dedicated GPU path;
3. allow Box3D CPU backend to upload compact poses instead of matrices;
4. switch WebGPU physics to direct buffers;
5. retain CPU snapshots only for tests, overlays, and asynchronous debugging;
6. remove the 80-byte CPU model-matrix path after all backends have replacements.

### 29.5 Overlay objects

The throwable preview and object-count overlay should live in a small separate CPU/GPU overlay buffer. Do not append presentation-only objects into authoritative physics arrays.

---

## 30. Queries, events, and readback

### 30.1 Query categories

- synchronous CPU character terrain queries;
- asynchronous GPU ray casts;
- asynchronous GPU overlap/shape casts;
- server-side authoritative queries;
- debug snapshots;
- event readbacks.

### 30.2 Readback ring

Use multiple mapped/readback buffers in a ring. A request records the submission/tick. Polling returns only completed results. Normal rendering and physics MUST NOT block on readback.

### 30.3 Query ordering

Multi-hit query results are sorted by:

```text
fraction/distance, body ID, shape/feature ID
```

This provides deterministic callbacks and replay comparisons.

---

## 31. Capacity model and memory budgeting

Every world configuration declares capacities:

```cpp
struct PhysicsCapacity {
    uint32_t residentBodies;
    uint32_t activeBodies;
    uint32_t commandsPerTick;
    uint32_t gridEntries;
    uint32_t candidatePairs;
    uint32_t uniquePairs;
    uint32_t contacts;
    uint32_t manifolds;
    uint32_t terrainContacts;
    uint32_t overflowConstraints;
    uint32_t events;
    uint32_t visibleBodies;
};
```

The overlay reports current count, capacity, high-water mark, and overflow flag for every category.

Suggested initial profiles:

| Profile | Resident | Active | Pair/contact intent |
|---|---:|---:|---|
| Browser low | 25k | 10k | small local scenes |
| Browser medium | 100k | 50k | ordinary desktop/browser |
| Desktop high | 1M | 100k–250k | many sleeping/resident bodies |
| Server worker | configurable | batched across worlds | workload dependent |

These are starting profiles, not promises.

At initialization, query adapter limits and clamp or reject unsupported profiles with a clear diagnostic.

---

## 32. Build and repository layout

### 32.1 Box3D dependency

Add a pinned Box3D checkout to `scripts/fetch_deps.sh`:

```text
repository: https://github.com/erincatto/box3d.git
commit: d421e45c828f6f853a145f726f0b9425d31146eb
```

Do not track Box3D `main` implicitly. Upgrades require:

- explicit commit change;
- release-note review;
- reference test rerun;
- benchmark comparison;
- determinism hash update only with documented reason.

Add Bazel and CMake targets. Preserve MIT notices.

### 32.2 CPU floating-point flags

The Box3D reference backend should use precise floating-point settings consistent with its determinism contract:

- no `-ffast-math`;
- floating-point contraction disabled where supported;
- matching SIMD configuration for recorded golden hashes;
- explicit build metadata in replay files.

### 32.3 Proposed source layout

```text
src/physics/
  physics_world.hpp                 public facade
  physics_world.cpp
  physics_types.hpp
  physics_commands.hpp
  physics_backend.hpp               internal backend interface
  physics_stats.hpp
  terrain_topology.hpp
  terrain_topology.cpp

  jolt/
    jolt_backend.hpp
    jolt_backend.cpp

  box3d/
    box3d_backend.hpp
    box3d_backend.cpp
    box3d_conversions.hpp

  gpu/
    gpu_physics_backend.hpp
    gpu_physics_backend.cpp
    gpu_physics_config.hpp
    gpu_buffer_arena.hpp
    gpu_buffer_arena.cpp
    gpu_pipeline_set.hpp
    gpu_pipeline_set.cpp
    gpu_render_view.hpp
    gpu_readback_ring.hpp
    gpu_readback_ring.cpp

  character/
    character_mover.hpp
    character_mover.cpp
    plane_solver.hpp
    plane_solver.cpp

  deterministic/
    fixed.hpp
    fixed_vec.hpp
    fixed_quat.hpp
    deterministic_math.hpp
    state_hash.hpp
    replay_format.hpp
    replay_writer.cpp
    replay_reader.cpp

shaders/physics/
  common_types.wgsl
  common_math.wgsl
  common_shapes.wgsl
  common_terrain.wgsl
  apply_commands.wgsl
  compact_active.wgsl
  scan_*.wgsl
  radix_*.wgsl
  build_grid_entries.wgsl
  build_cell_ranges.wgsl
  count_pairs.wgsl
  emit_pairs.wgsl
  unique_pairs.wgsl
  merge_contacts.wgsl
  bucket_pairs.wgsl
  narrow_sphere_sphere.wgsl
  narrow_sphere_capsule.wgsl
  narrow_sphere_box.wgsl
  narrow_capsule_capsule.wgsl
  narrow_capsule_box.wgsl
  narrow_box_box.wgsl
  narrow_convex.wgsl
  narrow_terrain.wgsl
  reduce_manifolds.wgsl
  build_islands.wgsl
  color_constraints.wgsl
  compact_colors.wgsl
  prepare_constraints.wgsl
  integrate_velocity.wgsl
  warm_start_static.wgsl
  warm_start_color.wgsl
  solve_static.wgsl
  solve_color.wgsl
  overflow_constraints.wgsl
  overflow_gather.wgsl
  integrate_position.wgsl
  restitution.wgsl
  store_impulses.wgsl
  update_sleep.wgsl
  ccd_fast_bodies.wgsl
  build_visible_lists.wgsl
  build_indirect_draws.wgsl
  hash_state.wgsl

server/
  physics_cuda/                     future, not first milestone
    ...
```

The exact number of shaders may be reduced by multiple entry points in shared modules. Keep entry points small enough to validate and profile independently.

### 32.4 Shared constants

Authoritative constants should be generated from one source where practical. Options:

- a small Python code generator emitting C++ and WGSL;
- a constrained header format parsed at build time;
- duplicated constants with compile-time and runtime parity tests.

Do not manually scatter slop, timestep, shape dimensions, bit masks, and fixed-point scales through many files.

### 32.5 Bazel and CMake parity

Every new library, shader, test, and benchmark MUST be represented in both build systems. Bazel remains the development source of truth; CMake must stay functional for integration.

---

## 33. Testing strategy

### 33.1 Preserve current tests

Existing Jolt tests remain and should be parameterized across backends where behavior is backend-independent.

Rename tests that hard-code “Jolt” in the test name or expectation when they become backend-generic.

### 33.2 CPU Box3D reference tests

Add tests for:

- all current throwable shapes;
- canonical terrain mapping and diagonal;
- character mover parity;
- water extension behavior;
- sleeping and wake;
- stable body handles;
- creation/destruction ordering;
- replay and state hashes.

### 33.3 GPU primitive tests

Before full physics, test:

- scan against CPU reference;
- stable radix sort against CPU reference;
- pair unique;
- sorted merge;
- deterministic compaction;
- fixed workgroup-size variants;
- overflow behavior;
- shader struct-size/layout parity.

### 33.4 Narrow-phase differential tests

For every shape pair:

- randomized transforms within bounded ranges;
- separated, touching, penetrating, degenerate, and nearly parallel cases;
- compare normal, separation, anchors, feature IDs, and point count to CPU oracle within tolerance;
- exact topology checks in lockstep mode;
- invariant checks: finite values, normalized normals, non-negative impulse limits.

### 33.5 Solver scenarios

Required scenes:

- single sphere drop;
- sphere pyramid;
- box tower;
- mixed-shape pile;
- capsule avalanche;
- cylinder stack;
- long thin body gyroscopic rotation;
- shallow and steep terrain slopes;
- terrain diagonal traversal;
- stair/edge motion;
- shoreline and floating equilibrium;
- high-speed projectile;
- repeated sleep and wake;
- one body touching hundreds of bodies;
- graph-color overflow;
- body creation/destruction during activity;
- sector boundary crossing.

### 33.6 Invariants

Every debug/CI simulation checks:

- no NaN or infinity;
- quaternion norm within bound;
- bounded penetration;
- no negative accumulated normal impulse;
- pair/contact/manifold count within capacity;
- graph colors conflict-free;
- body handles valid;
- active and sleeping sets disjoint;
- state hash stable for the expected mode.

### 33.7 Browser matrix

Run at least:

- Chromium WebGPU on a supported Linux/Windows environment;
- Firefox WebGPU when supported by CI;
- native Vulkan;
- native D3D12 where CI is available;
- native Metal where CI is available.

Strict cross-backend hash equality is required only after Lockstep is implemented. DeterministicFloat records per-certified-backend goldens.

---

## 34. Deterministic recording, replay, and hashing

### 34.1 Recording model

Adopt the Box3D concept:

```text
canonical checkpoint + ordered mutating commands = reproducible future state
```

Unlike Box3D's internal raw-struct snapshot format, Voxys should use a versioned canonical schema:

- explicit little-endian encoding;
- no raw pointers;
- no compiler padding;
- no GPU object handles;
- explicit arithmetic mode;
- explicit backend/shader version;
- explicit capacity profile;
- terrain/generator/content hashes;
- command stream;
- periodic state hashes and optional keyframes.

### 34.2 Checkpoint contents

A complete physics checkpoint includes:

- body handles/generations;
- body poses and velocities;
- shape and material data;
- active/sleeping partition;
- persistent contacts and manifolds;
- warm-start impulses;
- graph colors if retained;
- island roots and sleep counters;
- free lists;
- deterministic water phase/state;
- pending canonical commands;
- world tick;
- random stream state if gameplay uses deterministic randomness.

### 34.3 Hash levels

Compute hashes for:

- individual body state;
- persistent contact/manifold state;
- island state;
- region state;
- full world state.

Optional debug builds compute stage hashes after:

- commands;
- broad phase;
- pair unique;
- narrow phase;
- color assignment;
- every solver substep;
- sleeping;
- final state.

This identifies the first divergence rather than only reporting a final mismatch.

### 34.4 Replay tests

Replay the same recording with:

- different CPU worker counts;
- different GPU workgroup-size tuning profiles;
- subgroup optimization enabled/disabled;
- rendering enabled/disabled;
- debug instrumentation enabled/disabled;
- different command producer thread ordering before canonical sort.

Semantic hashes must satisfy the selected determinism contract.

---

## 35. Benchmark plan

### 35.1 Baselines

Measure:

- multithreaded Jolt with its automatic native worker count;
- single-threaded Jolt as a scheduler-scaling control;
- pinned Box3D with 1, 2, 4, 8, and available worker counts;
- WebGPU DeterministicFloat;
- later Lockstep;
- later CUDA server backend.

### 35.2 Existing Voxys benchmarks

Retain and extend:

- 10,000 body snapshot benchmark;
- 16,384 body maximum benchmark;
- water overhead benchmark;
- primitive culling benchmark;
- primitive instance upload benchmark.

### 35.3 Imported/reference scenes

Recreate representative Box3D-style scenes:

- large pyramid;
- rain/contact churn;
- junkyard/mixed shapes;
- high-degree overflow scene.

Do not compare timings unless scene geometry, timestep, substeps, material constants, and output requirements are documented.

### 35.4 Voxys-specific scenes

- `projectile_storm`: many active simple bodies over terrain;
- `terrain_avalanche`: mixed bodies contacting heightfield;
- `shoreline_debris`: terrain, water, and body contacts;
- `sparse_million`: one million resident, mostly sleeping bodies;
- `dense_pile`: increasing contact density;
- `island_swarm`: many small disconnected islands;
- `large_island`: one connected pile;
- `color_hub`: one high-degree body;
- `render_all`: body physics plus culling and primitive rendering;
- `headless`: simulation only.

### 35.5 Required timing breakdown

Use timestamp queries where supported and CPU timers otherwise:

- command application;
- active compaction;
- terrain contact generation;
- grid generation;
- radix sort;
- pair generation/unique;
- contact merge;
- narrow phase per shape bucket;
- manifold reduction;
- island build;
- coloring;
- prepare;
- each solver stage;
- overflow;
- sleeping/CCD;
- culling/indirect draw build;
- render;
- CPU submission and readback overhead.

### 35.6 Performance targets

Targets are evaluated on named hardware and browser versions.

| Milestone | Target |
|---|---|
| GPU ballistic/static | 100k active simple bodies, terrain/water, 60 Hz simulation |
| GPU sphere contacts | 100k active spheres with sparse contacts, 60 Hz |
| Mixed shape useful engine | 50k–100k active mixed bodies, scene dependent |
| Resident scale | 1M resident bodies with bounded active subset |
| Render transfer | no full body CPU readback or matrix upload |
| Browser low tier | graceful lower capacity, same authoritative constants |

A failed performance target does not justify removing determinism or correctness silently. Profile first, then make a documented trade.

---

## 36. Debugging and telemetry

Extend the debug overlay with:

- backend and arithmetic mode;
- physics tick and substep;
- resident, active, sleeping, and kinematic bodies;
- body memory bytes;
- terrain contacts;
- grid entries;
- occupied cells;
- candidate and unique pairs;
- active contacts and manifolds;
- average/max contacts per body;
- active graph colors;
- overflow constraints;
- island count and size percentiles;
- bullet/CCD count;
- water sample count;
- event count;
- visible/submitted bodies;
- GPU upload and readback bytes;
- per-stage GPU timings;
- all capacity high-water marks;
- current world/island hash;
- device limits and selected tuning profile.

Add debug visualizations for:

- body AABBs;
- sparse grid cells;
- candidate pairs;
- contact points/normals;
- terrain triangle features;
- graph colors;
- islands;
- sleeping bodies;
- CCD sweeps;
- water sample points;
- rollback corrections.

---

## 37. Distributed multiplayer architecture

This section is future-facing but shapes data, determinism, command, replay, and island APIs now.

### 37.1 Trust rule

> Clients may predict, generate, render, cache, and provisionally simulate. The server commits outcomes that affect other players, damage, ownership, inventory, economy, or persistence.

Clients send inputs and commands, not authoritative transforms, hit claims, or damage outcomes.

### 37.2 Logical services

```text
World coordinator
  - sessions, ticks, authority epochs, routing, persistence

Authoritative physics workers
  - CPU or native GPU simulation of assigned islands/regions

Realtime gateway
  - WebTransport/WebRTC, authentication, rate limiting

Browser clients
  - WebGPU prediction, rendering, static generation, visual physics
```

### 37.3 Three different partitions

#### Static generation chunks

Contain deterministic terrain, ocean parameters, voxel fields, static structures, materials, and collision metadata. Key:

```text
world seed, generator version, chunk coordinate, content hash
```

#### Interest cells

Determine which entities and events a client receives and at what rate. Interest does not determine authority.

#### Physics islands

Connected components of active dynamic contacts and joints. Physics islands determine authoritative simulation ownership and rollback units.

These three concepts MUST NOT be collapsed into one “chunk” abstraction.

### 37.4 Island authority

At a given tick, no contact or joint may write bodies owned by different authoritative workers.

```cpp
struct IslandAuthority {
    uint64_t islandId;
    uint32_t epoch;
    uint32_t workerId;
    uint64_t startTick;
    Hash128 checkpointHash;
};
```

Packets or commands using an old epoch are ignored.

### 37.5 Cross-worker approach and migration

Workers publish swept boundary proxies for the next horizon. Before two authoritative islands can contact:

1. detect possible cross-worker overlap;
2. select destination worker using deterministic tie-breaks plus load policy;
3. schedule migration at future tick T;
4. send a complete island checkpoint;
5. destination restores and optionally shadow-simulates;
6. compare hashes during handoff;
7. switch authority epoch at T;
8. source retains a rollback copy for a bounded window.

Use migration hysteresis to avoid ping-pong.

### 37.6 Authority classes

- **Class A:** consequential server-authoritative physics: players, ships, combat projectiles, damage, inventory objects.
- **Class B:** persistent but sleeping server-authoritative state: parked vehicles, wrecks, dropped valuable items.
- **Class C:** audited client leases for isolated low-consequence islands; results are provisional until server validation.
- **Class D:** client-only visual physics: spray, dust, tiny debris, foam, vegetation, distant fragments.

Client hashes do not prove honesty. They detect divergence and provide evidence; consequential state remains server committed.

### 37.7 Prediction bubble

Each client maintains rollback history only for a local prediction bubble:

- controlled actor/vehicle;
- nearby bodies likely to contact it;
- current local island;
- read-only ghosts near the boundary;
- buffered local inputs.

Starting history: 32–64 ticks.

When authoritative state for tick T arrives:

1. compare island hash;
2. if equal, confirm and discard older history;
3. if different, restore authoritative checkpoint at T;
4. replay local inputs to present;
5. smooth only presentation, never authoritative state.

Remote bodies outside the bubble are interpolated.

### 37.8 Transport

#### Primary: WebTransport

Use datagrams for:

- input frames;
- snapshots/deltas;
- tick and clock sync;
- acknowledgements;
- hashes;
- transient events.

Use reliable streams for:

- login/session control;
- entity creation/destruction;
- authority transfer;
- full island checkpoints;
- persistent chunk deltas;
- inventory/economy;
- replay files.

#### Fallback: WebRTC DataChannel

Recommended channel roles:

- unordered, zero-retransmit realtime channel;
- unordered reliable event channel where appropriate;
- ordered reliable control channel.

Keep realtime packets near conservative path MTU sizes. Fragment large checkpoints over reliable streams rather than one giant datagram.

### 37.9 Packet basics

Realtime packet header should include:

```text
protocol version
session/world ID and epoch
sequence
ack sequence and ack bitfield
tick
payload type
payload length
```

Input packets repeat recent frames so an isolated loss does not require retransmission.

Snapshot deltas reference an explicitly acknowledged baseline.

### 37.10 Interest and client tiers

Clients may simulate and render different amounts, but authoritative outcomes and constants remain the same.

- low tier: predict controlled actor only;
- medium tier: local interaction bubble;
- high tier: larger prediction bubble and more visual physics;
- optional very-high tier: audited Class C work and immutable chunk caching.

Capability is established through a startup benchmark and device limits, not trusted self-reporting.

### 37.11 Replicate causes, not every transform

Prefer events such as:

```text
spawn tick
initial pose
linear/angular velocity
shape/material seed
```

Clients predict until collision, correction, mode transition, or periodic checkpoint. This is especially effective for meteors, cannonballs, drifting debris, and isolated vehicles.

### 37.12 Procedural infinite world

Persistent world representation:

```text
deterministic base generator(seed, version, chunk coordinate)
+ ordered server-authored chunk delta log
+ sleeping dynamic checkpoints
+ active authoritative islands
```

Clients may generate visual chunks. The server derives or retrieves canonical collision chunks before consequential interaction. Client-supplied collision geometry is not trusted.

### 37.13 Server GPU batching

A server GPU should batch active islands from many worlds:

```text
collect active islands
  -> bucket by stage/shape pair/profile
  -> large compute launches
  -> scatter results by world/island
```

Do not launch one tiny CUDA workload per room if batching can amortize overhead.

### 37.14 Anti-cheat

Validate:

- input rate and sequence;
- movement/control limits;
- weapon cooldown/ammunition;
- ownership and interaction distance;
- authority epoch;
- world/chunk hashes;
- damage source;
- impossible energy or transform deltas;
- repeated prediction divergence.

Do not punish occasional divergence automatically; browser throttling, device loss, packet loss, and driver differences can be benign.

---

## 38. Structural assemblies for voxel-built machines

A ship, train, crane, or building made from thousands of blocks should not normally be thousands of internally colliding rigid bodies.

Represent an intact assembly as:

```text
one rigid body
+ aggregate mass and inertia
+ compact collision hulls/boxes
+ structural connectivity graph
+ subsystem and damage metadata
+ rendering instance graph
```

Damage removes structural edges. At a deterministic tick boundary:

1. compute connected components;
2. retain the main component on the original handle when policy allows;
3. create new rigid bodies for significant detached components;
4. recompute mass, center, inertia, collision aggregates, and buoyancy samples;
5. convert tiny fragments to grouped debris or Class D visual debris;
6. broadcast a deterministic fracture event.

This architecture is essential for Wreckwater-like ships, Iron Wake trains, cranes, towers, and leviathans.

Full arbitrary fracture is not required. Connections may be authored hardpoints or graph edges with health and material properties.

---

## 39. Detailed implementation phases

Each phase has required deliverables and an exit gate. Do not begin later destructive migration before earlier gates pass.

### Phase 0 — Baseline, instrumentation, and repository preparation

#### Tasks

- [x] Record the reviewed Voxys baseline commit and benchmark environment.
- [x] Add a backend field to benchmark output.
- [x] Split current frame timing into Jolt update, water work, snapshot copy, CPU culling, instance packing, upload, and render.
- [x] Add a multithreaded Jolt benchmark configuration without changing default gameplay behavior.
- [x] Preserve 10k and 16,384 body golden fixtures.
- [x] Add capacity and memory reporting for current Jolt and primitive buffers.
- [x] Add canonical terrain topology tests for height formula, origin, and TL-BR diagonal.
- [x] Add documentation links from `SPECS.md`.

#### Exit gate

A reproducible benchmark report exists for native and WASM where possible, and the current behavior is protected by tests.

### Phase 1 — Backend facade and pinned Box3D CPU reference

#### Tasks

- [x] Introduce `IPhysicsBackend` and keep `PhysicsWorld` as facade.
- [x] Move current Jolt code into `src/physics/jolt/` with behavior unchanged.
- [x] Add backend selection to application config and command line.
- [x] Pin Box3D commit `d421e45...` in dependency scripts.
- [x] Add Bazel and CMake Box3D targets.
- [x] Implement `Box3DReferenceBackend` for sphere, box, capsule, cylinder hull, and heightfield.
- [x] Preserve the Voxys terrain diagonal; adapt or cook terrain accordingly rather than assuming Box3D's diagonal.
- [x] Implement water as a Voxys extension.
- [x] Implement or adapt the geometric capsule character mover.
- [x] Parameterize physics tests across Jolt and Box3D where meaningful.
- [x] Import representative Box3D benchmark scenes.

#### Exit gate

Box3D backend runs native and WASM, passes backend-generic tests, and produces documented performance/correctness differences from Jolt.

### Phase 2 — GPU-resident body store and direct rendering

#### Tasks

- [x] Add GPU physics configuration and buffer arena.
- [x] Implement body handles, generation buffer, free-list management, and command application.
- [x] Implement active-body compaction.
- [x] Implement gravity, force, damping, quaternion integration, and speed clamps without collision.
- [x] Implement debug readback ring.
- [x] Add `PhysicsRenderView`.
- [x] Modify primitive shader/path to render from body IDs, compact poses, quaternions, and dimensions.
- [x] Add GPU frustum culling and per-shape indirect draws.
- [x] Keep Jolt/Box3D compatibility through compact pose uploads.
- [x] Remove full model-matrix uploads from the WebGPU backend path.

#### Exit gate

At least 100,000 ballistic bodies update and render with no normal-frame CPU body snapshot or model-matrix upload.

### Phase 3 — Heightfield and physical water static contacts

#### Tasks

- [x] Implement shared terrain topology constants/helpers.
- [x] Implement max-height-mip rejection.
- [x] Implement sphere-terrain contacts.
- [x] Implement box-terrain contacts.
- [x] Implement capsule-terrain contacts.
- [x] Implement cylinder-hull terrain contacts or a bounded convex fallback.
- [x] Implement four-point terrain manifold reduction.
- [x] Implement internal edge suppression.
- [x] Implement body-local static Soft Step solve.
- [x] Fuse water sampling/buoyancy/drag into velocity integration.
- [x] Add shoreline, slope, diagonal, and floating tests.

#### Exit gate

100,000 simple bodies can fall, settle, slide, roll, and float over the heightfield with stable rendering and no body-body collision.

### Phase 4 — GPU deterministic primitives

#### Tasks

- [x] Implement `u32` scan.
- [x] Implement stable compaction.
- [x] Implement stable radix sort for 32-bit and logical 64-bit keys.
- [x] Implement adjacent unique.
- [x] Implement sorted merge.
- [x] Implement deterministic free-ID assignment.
- [x] Add randomized CPU/GPU differential tests.
- [x] Test multiple workgroup sizes and subgroup fallback.

#### Exit gate

All primitives match CPU reference output exactly for large randomized fixtures and deterministic overflow tests.

### Phase 5 — Dynamic broad phase and persistent contact lifecycle

#### Tasks

- [x] Implement sparse grid entry counting/scatter.
- [x] Implement occupied-cell range construction.
- [x] Implement forward-neighbor pair counting/scatter.
- [x] Sort and unique canonical body pairs.
- [x] Implement active-vs-sleeping queries.
- [x] Implement oversized-body fallback.
- [x] Implement persistent contact sorted merge.
- [x] Implement deterministic begin/end events.
- [x] Expose grid/pair debug visualization and high-water metrics.

#### Exit gate

Pair sets match a CPU brute-force oracle for randomized bounded scenes and remain stable across repeated runs and tuning profiles.

### Phase 6 — Dynamic narrow phase and persistent manifolds

#### Tasks

- [x] Sphere-sphere.
- [x] Sphere-capsule.
- [x] Capsule-capsule.
- [x] Sphere-box.
- [x] Capsule-box.
- [x] Box-box face/edge SAT and clipping.
- [x] Fixed cylinder convex hull and required pair paths.
- [x] Persistent feature IDs and point matching.
- [x] Four-point reduction.
- [x] Weighted friction center.
- [x] Differential tests against Box3D/reference geometry.

#### Exit gate

All current Voxys shape pairs produce finite, stable, persistent manifolds and pass randomized differential/invariant tests.

### Phase 7 — Dynamic graph coloring and Soft Step solver

#### Tasks

- [x] Implement deterministic full recoloring.
- [x] Validate conflict-free color ranges.
- [x] Implement constraint preparation.
- [x] Implement dynamic warm start.
- [x] Implement biased normal solve by color.
- [x] Implement position integration.
- [x] Implement no-bias relaxation.
- [x] Implement central friction, twist, and rolling resistance.
- [x] Implement restitution and impulse storage.
- [x] Implement overflow endpoint-delta/gather path.
- [x] Add gyroscopic correction.
- [x] Add persistent color optimization.
- [x] Add small-island workgroup fast path after global correctness.

#### Exit gate

Box towers, mixed piles, and avalanches are stable; overflow is measured; 100k sparse-contact spheres meet the target hardware budget; no body-state atomic float operations exist.

### Phase 8 — Islands, sleeping, CCD, character integration, and queries

#### Tasks

- [x] Deterministic island union-find and compaction.
- [x] Island sleep/wake state.
- [x] Persistent sleeping grid.
- [x] Fast sphere/capsule terrain CCD.
- [x] Bounded bullet path.
- [x] CPU character mover using shared topology.
- [x] Nearby dynamic-body interaction policy.
- [x] Async ray/overlap/shape queries.
- [x] Deterministic events and readback ring.

#### Exit gate

Long-running scenes sleep and wake correctly, bullets do not commonly tunnel through terrain, and character movement does not require synchronous GPU world readback.

### Phase 9 — Deterministic replay and Lockstep mode

#### Tasks

- [x] Canonical versioned checkpoint schema.
- [x] Ordered command recording.
- [x] Body/contact/island/world hashes.
- [x] Replay player and divergence diagnostics.
- [x] DeterministicFloat certification matrix.
- [x] Fixed-point scalar CPU implementation.
- [x] Fixed-point WGSL arithmetic primitives.
- [x] Lockstep body integration.
- [x] Lockstep topology decisions, contact generation, and solver.
- [x] Cross-backend hash CI.

#### Exit gate

A selected deterministic corpus replays exactly under the declared contract. Lockstep produces identical hashes across certified backends.

### Phase 10 — Server-authoritative browser networking

#### Tasks

- [x] Canonical network command schema.
- [x] WebTransport gateway.
- [x] WebRTC DataChannel fallback.
- [x] Tick synchronization and input redundancy.
- [x] Authoritative snapshots and acknowledged deltas.
- [x] Client prediction bubble.
- [x] Island-local rollback and replay.
- [x] Interest cells.
- [x] Authority epochs.
- [x] Two-client meteor/box sandbox.
- [x] Recording of all commands and correction events.

#### Exit gate

Two browser clients interact in one server-authoritative world with local prediction, island rollback, replayable commands, and no trusted client transforms.

### Phase 11 — Distributed workers and native server GPU

#### Tasks

- [x] World coordinator and worker assignment.
- [x] Swept boundary proxy exchange.
- [x] Deterministic island migration.
- [x] Dual-run or shadow handoff with hash verification.
- [x] Native GPU backend, preferably CUDA first if target hardware is NVIDIA.
- [x] Shared arithmetic/algorithm contract with WebGPU.
- [x] Multi-world and multi-island batching.
- [x] Load, migration, and rollback telemetry.
- [x] Fault injection and worker-loss recovery.

#### Exit gate

Connected islands never span authoritative workers during a tick, migration preserves hashes, and server batching outperforms isolated per-room simulation.

### Phase 12 — Jolt retirement and product vertical slices

#### Tasks

- [x] Run complete native/WASM parity suite.
- [x] Verify CPU fallback story.
- [x] Verify Box3D reference remains available for debugging if desired.
- [ ] Remove Jolt dependency only after explicit approval.
- [x] Build Demolition League engine-validation slice.
- [x] Build Deadweight co-op transport slice.
- [x] Prototype Wreckwater assembly, buoyancy, and fracture slice.
- [ ] Choose product based on playtest evidence, not only engine spectacle.

#### Exit gate

No supported configuration requires Jolt, and at least one bounded game slice is repeatedly fun and operationally supportable.

---

## 40. Pull request sequencing and agent rules

Implementation agents MUST follow these rules:

1. Do not implement multiple major phases in one PR.
2. Preserve the current Jolt backend until its retirement gate.
3. Every new GPU algorithm includes a CPU reference test or invariant test.
4. Every new buffer includes capacity, memory, high-water, and overflow reporting.
5. Every semantic list uses canonical ordering.
6. No normal-frame full-body readback is introduced.
7. No race-dependent atomic append is accepted for ordered physics data.
8. No unbounded GPU loop is accepted in authoritative kernels.
9. Bazel and CMake changes land together.
10. Native and WASM shader compilation are both tested.
11. New authoritative constants are centralized and parity-tested.
12. Performance claims include scene, hardware, backend, browser/driver, timestep, substeps, and active contact count.
13. A faster result that changes the determinism contract must be a named mode, not a silent optimization.
14. Update this document's checklist or decision log when architecture changes.
15. Keep PRs bisectable; each merged commit should build or be clearly guarded behind disabled configuration.

Suggested PR identifiers:

```text
GP-000 baseline instrumentation
GP-100 backend facade
GP-110 Box3D dependency
GP-120 Box3D backend
GP-200 GPU body store
GP-210 direct render path
GP-300 terrain contacts
GP-310 physical water
GP-400 scan/compaction
GP-410 radix sort
GP-500 sparse broad phase
GP-510 persistent contacts
GP-600 narrow phase common shapes
GP-610 box/cylinder manifolds
GP-700 graph coloring
GP-710 Soft Step solver
GP-720 overflow and islands
GP-800 sleeping/CCD/queries
GP-900 replay and hashes
GP-910 lockstep arithmetic
NET-100 authoritative two-client sandbox
NET-200 rollback and interest
NET-300 worker migration
CUDA-100 server backend
```

---

## 41. Acceptance criteria before Jolt removal

All of the following are required:

### Correctness

- current backend-generic physics tests pass;
- terrain topology matches rendering exactly;
- all current shapes work;
- water behavior works;
- character controller works;
- no capacity overflow in supported profiles/scenes;
- no NaN/infinite state under fuzz tests;
- sleeping, wake, and CCD scenarios pass;
- replay divergence diagnostics exist.

### Platform

- native Vulkan path passes;
- Windows native path passes where supported;
- macOS native path passes where supported;
- browser WASM/WebGPU path passes;
- device-limit fallback produces clear errors or lower profiles.

### Performance

- GPU backend materially beats the current CPU snapshot/upload path for large active scenes;
- multithreaded Jolt and Box3D baselines are included;
- normal rendering has no full body readback or model-matrix upload;
- target scenes meet agreed frame budgets on named hardware.

### Maintainability

- backend abstraction is stable;
- build files are clean;
- shader modules have unit/differential coverage;
- capacities and telemetry are documented;
- Box3D attribution is preserved;
- no undocumented vendor-only dependency is required for browser operation.

---

## 42. Risk register

### Risk: WebGPU floating-point differences

**Impact:** cross-vendor hash divergence.  
**Mitigation:** separate DeterministicFloat and Lockstep contracts; canonical ordering; fixed-point strict mode; server authority.

### Risk: too many global color dispatches

**Impact:** dispatch overhead dominates small islands.  
**Mitigation:** indirect zero-work colors; small-island workgroup path; persistent colors; profile 24 vs 32 colors.

### Risk: radix sort cost dominates sparse scenes

**Impact:** CPU backend or tree may be faster at low counts.  
**Mitigation:** backend thresholds; persistent sleeping grid; avoid sorting unchanged data; benchmark grid entry count; optional hybrid path.

### Risk: dense piles explode pair/contact counts

**Impact:** capacity overflow and poor performance.  
**Mitigation:** explicit profiles, high-water metrics, sleeping, contact persistence, better cell sizing, assembly aggregation, deterministic overflow failure.

### Risk: graph-color overflow

**Impact:** lower solver convergence and gather cost.  
**Mitigation:** static bypass, sufficient color count, persistent colors, high-degree diagnostics, scene design constraints.

### Risk: terrain/render collision mismatch

**Impact:** visible hovering, sinking, or snagging.  
**Mitigation:** shared topology module, canonical TL-BR diagonal, parity tests, no blind Box3D heightfield reuse.

### Risk: Box3D changes rapidly

**Impact:** unstable oracle and moving behavior.  
**Mitigation:** pin commit, explicit upgrade process, retain goldens, document imported algorithms.

### Risk: CPU character and GPU bodies disagree

**Impact:** visible penetration or latency.  
**Mitigation:** shared terrain constants, local body mirror, next-tick impulses, limited interaction set, correction smoothing.

### Risk: browser buffer/device limits

**Impact:** initialization failure on low-tier devices.  
**Mitigation:** capability profiles, adapter limit query, smaller capacities, CPU/Box3D fallback where viable.

### Risk: lockstep fixed-point cost is excessive

**Impact:** strict mode too slow.  
**Mitigation:** server authority does not require every client to use lockstep; keep fast float prediction; optimize integer ranges; restrict lockstep active bubble.

### Risk: CUDA and WGSL implementations diverge

**Impact:** rollback corrections and hard-to-debug server/client differences.  
**Mitigation:** common algorithm spec, generated constants, scalar CPU oracle, stage hashes, shared replay corpus.

### Risk: distributed world over-engineering before gameplay

**Impact:** years of infrastructure without a fun product.  
**Mitigation:** bounded vertical slices; Demolition League first; Deadweight next; networking begins with two-client meteor sandbox; no MMO requirement for first release.

---

## 43. Open decisions requiring measured evidence

### 43.1 Implemented decision record

The following choices are implemented and protected by differential, replay,
or benchmark tests:

| Decision | Implemented choice | Evidence |
|---|---|---|
| Dynamic colors | 32 colors with deterministic overflow gather | Phase 7 solver stress and 100k-body/50k-contact benchmark |
| Sparse-grid cell size | 4 m production default, configurable through `GpuConfig`; the size must evenly divide each 256 m sector | CPU brute-force pair oracle, signed-i32 toroidal-key coverage, and 100k sparse benchmark |
| Small-body grid insertion | One center-cell entry plus all 13 forward neighbor cells; bodies with diameter above one cell use the oversized path | Same pair oracle across workgroups 64/128/256; lower memory and frame time than bounded multi-cell insertion |
| Body storage | Split, aligned pose, motion, shape, metadata, generation, and force buffers | Native/WASM layout tests and direct-render path |
| Render pose ownership | Physics pose buffer is shared directly with rendering | 100k direct-render benchmark, zero normal-frame transform upload |
| Cylinder | Authored eight-sided convex hull | All ten narrow-phase pair-class tests and Box3D differential corpus |
| Terrain manifolds | Deepest point followed by deterministic spatial spread, capped at four | Terrain slope, diagonal, and five-shape settling tests |
| Small-island specialization | Two bodies and one contact | Byte-identical global-versus-local solver differential test |
| Portable workgroups | Runtime profiles 64, 128, and 256; 128 is the current stage default except the 256-wide body pipeline | Primitive and stage profile tests |
| Optional timestamp queries | Simulation remains available without them; profiling reports no batch | Context feature fallback and calibrated native benchmark |
| Native server compute | WebGPU/Vulkan first on the measured AMD host | Cross-oracle native-server test and 16-world batching benchmark |
| Character nearby-body policy | Terrain-only synchronous mover; dynamic interaction is deferred to commands/next tick | Character replay and no-world-readback tests |

The center-cell broad phase is an explicit deviation from the earlier
multi-cell insertion sketch. It is exact under the enforced common-body
invariant `2 * boundingRadius <= cellSize`; every other body is routed to the
oversized fallback. It reduced broad-phase grid scratch from
`bodyCapacity * maxEntriesPerBody` records to one record per body.

Product priority remains deliberately unresolved. The playtest ledger rejects
synthetic evidence and requires comparative, human-verified sessions.

The table above records the implemented choices. The original question set is
preserved below as an audit trail. Current release blockers are tracked in
[GPU Physics: Remaining Work](../docs/gpu-physics-remaining-work.md).

1. 24, 32, or adaptive graph color count?
2. Single-level sparse grid cell size for current shapes?
3. Multi-cell insertion cap before oversized fallback?
4. Coarse grid or LBVH for assemblies?
5. Exact GPU body buffer packing?
6. Separate pose buffers for rendering and simulation, or one shared layout?
7. Box3D cylinder hull side count?
8. Best terrain manifold reduction heuristic?
9. Whether physical float water samples existing FFT textures or a lower-resolution dedicated field?
10. Small-island workgroup body/constraint threshold per adapter tier?
11. Fixed-point formats after range analysis?
12. WebGPU scan/radix workgroup sizes?
13. Required browser fallback when timestamp queries or indirect features are unavailable?
14. Whether the native server backend begins with CUDA, Vulkan compute, or both?
15. How much nearby dynamic state the CPU character mirrors?
16. When to introduce kinematic bodies and joints?
17. Which first commercial vertical slice receives priority after engine validation?

---

## 44. Immediate next actions

All checked implementation tasks are complete. External validation and human
gates remain. The live release-gate list is [GPU Physics: Remaining
Work](../docs/gpu-physics-remaining-work.md).

Recommended sequence:

1. Add a composed terrain, water, dynamic-contact, and direct-render benchmark.
2. Measure it on a representative discrete desktop GPU.
3. Complete browser replay certification and the remaining platform matrix.
4. Resolve the Bazel aggregate-test sandbox issue.
5. Run comparative product playtests.
6. Request explicit approval before removing Jolt.

---

## 45. Reference index

### Voxys baseline files

- `src/physics/physics_world.hpp`
- `src/physics/physics_world.cpp`
- `src/physics/BUILD`
- `src/render/primitive_path.hpp`
- `src/render/primitive_path.cpp`
- `src/render/primitive_instance_packing.hpp`
- `src/render/primitive_instance_packing.cpp`
- `src/render/primitive_culling.hpp`
- `src/render/primitive_culling.cpp`
- `src/render/water_simulation.hpp`
- `src/render/water_simulation.cpp`
- `src/render/triangle_path.cpp`
- `shaders/terrain.wgsl`
- `shaders/terrain_raycast.wgsl`
- `shaders/physics_primitives.wgsl`
- `src/app/application.cpp`
- `tests/test_physics_world.cpp`
- `tests/physics_snapshot_benchmark.cpp`
- `tests/water_lock_benchmark.cpp`
- `scripts/fetch_deps.sh`
- `third_party/BUILD`
- `src/CMakeLists.txt`

### Box3D reference files at `d421e45...`

- `README.md`
- `include/box3d/constants.h`
- `src/constraint_graph.h`
- `src/constraint_graph.c`
- `src/solver.h`
- `src/solver.c`
- `src/contact_solver.h`
- `src/contact_solver.c`
- `src/broad_phase.h`
- `src/broad_phase.c`
- `src/height_field.c`
- `src/manifold.c`
- `docs/simulation.md`
- `docs/recording.md`
- `docs/large_worlds.md`
- `docs/character_mover.md`

### External references

- Box3D: `https://github.com/erincatto/box3d/tree/d421e45c828f6f853a145f726f0b9425d31146eb`
- GPU Gems 3 Chapter 29: `https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-29-real-time-rigid-body-simulation-gpus`
- Dennis Gustafsson solver talk: `https://www.youtube.com/watch?v=Kvsvd67XUKw`
- WGSL: `https://www.w3.org/TR/WGSL/`
- WebGPU: `https://www.w3.org/TR/webgpu/`
- WebTransport: `https://www.w3.org/TR/webtransport/`
- WebRTC Data Channels: `https://datatracker.ietf.org/doc/html/rfc8831`

---

## 46. Final architectural summary

The intended system is not “Jolt rewritten in WGSL.” It is a specialized Voxys physics platform with:

- Box3D as a pinned CPU reference and source of proven 3D solver ideas;
- GPU Gems' GPU-resident simulation and spatial-grid lessons;
- graph-colored in-place Soft Step solving for dynamic contacts;
- body-local static heightfield solving;
- stable sort/scan pipelines for deterministic parallel data construction;
- persistent four-point manifolds and central friction;
- a deterministic overflow gather path;
- sector-relative large-world coordinates;
- direct rendering from physics state;
- a CPU geometric character mover;
- separate fast float and strict lockstep contracts;
- explicit capacities, telemetry, replay, and cross-backend testing;
- server-authoritative island ownership, prediction, rollback, and future GPU worker batching;
- structural assemblies rather than uncontrolled per-voxel rigid-body explosions;
- bounded game vertical slices that prove fun before infrastructure scope expands.

The architecture should make extraordinarily large, physically expressive browser games possible without pretending that all devices, all floating-point implementations, or all clients are equally trustworthy. Correctness, determinism, performance, and product scope are treated as separate concerns with explicit contracts and gates.
