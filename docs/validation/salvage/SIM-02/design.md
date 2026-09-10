# SIM-02 — physics shape and mass contracts

Preparatory work claimed by root 2026-09-08. SIM-01's complete implementation
passes 78 shared cases and allocation probes; independent review is pending.
Existing review workers are authoritatively errored on account usage limits.
Preparatory work proceeds while all prerequisite/dependent acceptance and gate
commits remain open. No independent review or live physics capability is claimed.

## Observed backend constraints before stage-d1

`BodySpawnDesc` describes one ThrowableShape with dimensions and inverse mass.
GPU BodyShape is 48 bytes: dimensions/type, diagonal inverse inertia/material
and material coefficients. Ballistic spawning and several terrain paths derive
primitive inertia from dimensions; the dynamic solver and attachment paths
consume diagonal body-frame inertia. Its gyroscopic correction also assumes a
diagonal tensor. Existing poses use primitive centers, which coincide with COM.
Legacy LEGO compound selection uses the reserved material high nibble, and the
shape integer is also packed with a swept-distance fraction. These must not
be repurposed as authored compound identity or silently treated as full support.

## Required stages

1. Validated mass/frame preparation from the full authored tensor. Compute a
   proper principal-axis body frame centered at COM. The diagonal solver can
   then consume the full physical tensor without dropping off-diagonal terms.
   Root-space geometry and render transforms must retain an explicit inverse
   mapping; root axes remain the canonical authored axes.
2. Explicit authored shape identity and immutable resource contract, distinct
   from material flags. Bind root geometry/exterior patches/BVH/provenance and
   mass frame without changing legacy primitive/studded behavior. Specify
   request/generated/world and live-plus-retiring budgets independently.
3. Actual backend admission/upload and pose/force integration. Update every
   operation interpreting body-local points, root pose and COM velocity.
   Advertise support only once actual capability checks pass; unsupported CPU
   paths return explicit errors. No primitive fallback for rejected compounds.
4. Native/WASM numerical and actual GPU conformance for an asymmetric body,
   off-center forces, pose/root reconstruction and legacy regression evidence.
   SIM-03 separately carries compound contacts through every geometry consumer.

## Principal-body convention

For root-local dry COM `c` and symmetric tensor `I`, find a proper orthonormal
matrix `R` whose columns are principal body axes expressed in root coordinates,
with positive diagonal moments `D`: `I = R D R^T`.

`rootFromBody(p) = c + R p`; `bodyFromRoot(p) = R^T (p-c)`.
The body origin is COM, while its orientation is a derived physical frame.
World body orientation is world root orientation times R. Reconstruct authored
root orientation using the inverse R; reconstruct root origin by subtracting
the rotated COM offset. Linear velocity belongs to COM. A root-origin velocity
needs the explicit `omega × rotated c` shift. An attachment or force location
must subtract COM and rotate into principal axes exactly once.

The numerical foundation uses bounded fixed-size symmetric Jacobi iteration,
scale normalization, deterministic pair/order/tie rules, canonical axis signs
and a positive determinant. Repeated moments may admit multiple equivalent
bases; do not promise bitwise cross-platform eigenvectors or solver determinism.
Check reconstruction/orthogonality and physical principal-moment inequalities,
and reject nonfinite, nonsymmetric, singular or numerically unsupported input.
No catalog/engine state or live body is mutated by preparation or a failure.

Using principal axes preserves full-tensor physics while retaining diagonal
inertia multiplies in the hot solver. It does not permit treating canonical
root axes as principal axes, adding a second COM shift, or dropping body/root
transforms during collision, rendering, queries, attachments or save/replay.
Those integration changes and actual GPU evidence remain required.

## Stage-a1 numerical contract

`RigidMassFrame` now implements the first numerical stage using double precision,
fixed-size arrays and no heap allocation. It accepts finite positive mass with a
finite reciprocal, finite COM and finite symmetric full inertia. Symmetry is
checked after scale normalization (64 double epsilons); only that tiny accepted
roundoff is averaged. Cyclic (0,1), (0,2), (1,2) Jacobi sweeps perform at most 48
rotations, with explicit residual failure. Sorted moments have deterministic
exact-tie ordering. The first two eigenvector signs use their largest component
(lowest-index exact tie); the third axis preserves positive handedness.

Principal moments must be positive, physically obey the triangle inequality
(with 128 normalized epsilons of roundoff), and have minimum/maximum ratio above
1e-12. Inertia and reciprocal values must remain finite. Reconstruction,
orthogonality and determinant errors are explicitly bounded by 1,024 double
epsilons before returning the owned value. Invalid inputs produce a typed error
and no partial output. Existing caller-owned values remain untouched.

The value provides exact mathematical body/root point/vector/pose and COM/origin
velocity mappings plus principal-frame angular response and impulse-at-point
calculation. These helpers require caller-validated finite runtime inputs; they
are not a command admission, fixed-step integration or live activation API.
They also do not certify f32 representability. In particular, the extreme-scale
CPU tests exercise numerical normalization, not a promise to simulate those
scales on GPU. Subsequent packing must validate representable moments,
reciprocals, coordinates, quaternion conversion and error tolerances.

No backend capability is advertised and no existing GPU buffer ABI, shader,
primitive spawn or material selection changes at this stage. The normal core
builds include the preparation library; the separate WRECKWATER authority source
manifest stays unchanged. Shape resource identity, runtime admission/packing,
GPU force/pose integration, unsupported CPU rejection and every compound
consumer remain required before SIM-02/SIM-03 acceptance.

## Stage-b1 shape resource contract

`AuthoredShape` prepares one immutable root from the validated exact `BoxUnion`
and its full `RigidMassInput`. The common integer box/point and union algorithm
now live under `src/geometry/`; construction retains compatibility type aliases.
Physics does not import catalogs, durable IDs, inventory or authority. The union
algorithm, range restrictions, source labels, face ordering and canonical input
identity are unchanged. In particular, a fully enclosed cell has zero contact
patches; consumers must not manufacture its six box faces.

The format-1 prepared storage has explicit 16-byte-aligned rows:

| Record | Bytes | Contents |
|---|---:|---|
| `PackedShapeMass` | 48 | COM xyz/inverse mass; root-from-principal-body quaternion xyzw; principal inverse inertia xyz/conservative COM radius |
| `PackedShapeCell` | 48 | integer minimum xyz/source; integer maximum xyz/first face; face count/three zero words |
| `PackedShapeFace` | 48 | integer minimum xyz/source; integer maximum xyz/cell; axis/signed outward normal/two zero words |
| `PackedShapeNode` | 32 | integer minimum xyz/cell or UINT32_MAX; integer maximum xyz/preorder escape |

All geometry remains in authored root axes at .02 m per tick, not principal
body axes. All cell/face/node indices are local to this resource. GPU atlas
offsets and a generation/format-tagged descriptor table are subsequent upload
work; these CPU records do not change the legacy 48-byte `BodyShape` ABI.
Opaque source labels belong to the matching compiled assembly's provenance
table. The future body binding must retain that assembly/revision mapping for
as long as any body, GPU submission or asynchronous event needs it. A shape
handle or source integer alone is not a durable part ID or cache authorization.

Packing requires finite positive **normal** f32 mass, reciprocal mass, principal
moments and reciprocal moments. It verifies the float reciprocal used by the
gyroscopic path. COM lies within ±256 m per root axis and quantizes within
.0001 m. Quaternion conversion selects its largest squared component, including
half turns, normalizes in double and packs named xyzw. Sign canonicalization
uses positive w, then first nonzero xyz, with positive zeros.

With epsilon = f32 epsilon, the stored quaternion's squared norm must be within
4 epsilon of one; its reconstructed basis must be within 8 epsilon of the
prepared basis. Full inertia and inverse-inertia reconstruction are bounded
by 64 epsilon times their largest respective principal value. Their product
must be within .001 of the identity in every entry. This rejects cases where
quantization magnifies poor conditioning; no diagonal substitution or clamping
turns a rejected input into a body. These checks assess quantized input using
double reference arithmetic, not accumulated GPU integration error.

The radius encloses every root-bounds corner relative to the **quantized** COM.
It has a 16-epsilon relative margin plus .0002 m, rounded upward to f32. The
.0001 m COM tolerance is not a promise that every large rotated point has that
same absolute error. GPU transform/conservative-bound conformance remains to
be measured. Root/body point conversion subtracts COM and applies the inverse
stored basis exactly once; world sectors are not folded into these local data.

Per-root preparation caps are 4,096 cells, 24,576 exterior patches and 8,191 BVH
nodes; lower nonzero profiles are accepted. The existing compiler independently
bounds aggregate generated geometry and work across a request. Preparation
owns three exact-reserved vectors and returns a typed allocation/shape/mass/
representability/capacity failure with no partially published resource. An
already moved-from union is rejected. Public preparation takes a validated
union value; it is not an unchecked binary decoder for arbitrary cell buffers.

## Stage-b1 lifetime ledger and budgets

The formerly unused `ShapeHandle` now names `(index, generation, pool)` in a
standalone header. All three fields must be nonzero. The backend/session owner
must supply a unique nonzero 64-bit pool incarnation, never reuse it while old
handles can exist, and reject foreign-context handles. Moving a pool transfers
its identity and stable slot storage and invalidates the old owner. It cannot
be copied or move-assigned over another live pool. No body/material flags are
repurposed as a shape ID. GPU descriptors will carry index/generation within
the already selected pool.

`AuthoredShapePool` is a single-owner CPU ledger. Creation allocates its bounded
slot table; insertion moves already prepared storage only after checking all
limits. Failed insertion leaves the caller's value and pool unchanged.

| Limit | Default | Hard maximum |
|---|---:|---:|
| Resident plus retiring slots | 256 | 512 |
| Charged cells | 16,384 | 65,536 |
| Charged faces | 98,304 | 393,216 |
| Charged nodes | 32,768 | 131,072 |
| Charged shape bytes | 16 MiB | 32 MiB |
| Handles in one submission registration | 512 | 512 |
| Generations per slot | UINT32_MAX | UINT32_MAX; lower positive ceiling allowed |

Shape bytes charge actual vector capacities plus the fixed shape record.
`slotStorageBytes()` separately reports all preallocated table metadata; the
record charge deliberately conservatively overlaps its occupied record. Prepared
caller staging and future GPU buffers are separate budgets, not hidden inside
this byte count. The initial pool defaults are admission caps, not measured
launch-capacity or memory/performance acceptance. SIM-06 must reserve combined
old/new preparation, CPU/GPU and mapping costs before authority commit.

Lifetime protocol for the future backend owner:

1. Insert a prepared immutable shape. Retain it before holding a pointer across
   mutations/asynchronous readback. Retain counts are checked and do not wrap.
2. Before queue submission, atomically register its increasing nonzero serial
   and all resident shape handles. A bad/retiring/foreign handle or oversized
   batch changes nothing. Repeated handles are harmless; callers deduplicate
   many body references. An empty batch can track other work on the same queue.
3. Retire freezes new retains/submissions. Old retained readers can still read
   the immutable resource. Retiring slots and geometry continue to consume every
   budget. The owner must remove future body use before retiring its shape.
4. Advance completion only from certified completion of the same GPU queue and
   incarnation. Decreasing or not-yet-submitted serials reject. Equal completion
   is harmless. Submission serial exhaustion rejects further admission.
5. Collect only when the last registered GPU serial has completed **and** all
   retained readers have released. Then increase generation and make the slot
   reusable, or permanently exhaust it at its configured generation ceiling.
   A stale handle can never release, read or retire its replacement.

These operations allocate no memory after pool creation/preparation. If queue
encoding/submission fails after registering a use, retain it conservatively;
do not fabricate completion to reclaim it. Pool destruction is allowed only
after the owner drains dependent use or invalidates its entire device/context.
This ledger does not itself upload, submit, wait or observe a GPU fence. Its
tests use explicit model serials, not claims of actual queue completion. The
actual GPU allocation, descriptor uploads, facade admission, pose/force/frame
adaptation, CPU capability rejection and native/browser GPU conformance remain
the next SIM-02/SIM-03 work.

## Stage-c1 actual GPU shape resource

`GpuAuthoredShapeStore` now owns the stage-b1 CPU pool and a single immutable-
shape GPU atlas. It derives its queue from the supplied device: a caller cannot
pair one device's error scopes with another device's queue. This remains a
standalone resource component. `PhysicsWorld` and `GpuPhysicsBackend` have not
yet admitted or simulated an authored body; no capability bit changes.

The one `array<vec4<u32>>` read-only storage binding avoids spending four extra
storage bindings in already crowded physics pipelines. Format 1 consists of:

| Section | Layout |
|---|---|
| Header | 2 rows: format, slots, cell/face/node bases, total rows, cell/face capacities |
| Descriptors | 7 rows each, including permanently invalid slot 0: generation/format/cell span; face/node spans; 3 mass rows; root integer min/max |
| Cells/faces/nodes | The exact stage-b1 packed records; all per-shape references remain local indices |

The atlas is allocated once with Storage/CopyDst/CopySrc usage and WebGPU's
zero-initialization guarantee. Its exact byte count must fit both the requested
GPU budget (maximum 32 MiB) and the device's buffer/storage-binding limits.
The independent CPU budgets charge live plus retiring shapes. Three fixed
513-interval first-fit allocators reserve contiguous cell/face/node spans. All
three reservations and CPU admission pass before consuming the caller's shape.
Fragmentation is an explicit capacity refusal, even if aggregate free elements
would suffice. Retirement coalesces adjacent intervals; it does not compact or
move in-flight shapes.

Creation, uploads, external submissions and descriptor invalidations each
receive an internal monotonic serial and **execute a real queue submission**.
No public method accepts a guessed completion serial. Up to eight operations
can remain pending, with one unresolved external submission. Admission refuses
while that ticket is unresolved or the operation ring is full.

The owner calls `prepareSubmission(handles)` before encoding dependent work.
Every handle must be ready, belong to this pool, and match its generation.
The reservation records all uses before encoding and opens validation/OOM
scopes (plus Internal on the browser port). The pinned native implementation
aborts on Internal scopes, so its supported two filters are used. The owner
balances any nested device scopes and resolves the ticket on the same thread:

- `submit(ticket, commands)` submits 1–16 actual command buffers, pops scopes
  and registers actual queue completion callbacks.
- `discard(ticket)` requires all ticket encoders/commands to have been released;
  it submits an empty batch, pops scopes and observes a real completion fence.
  Unfinished encoders can defer validation until finish on Dawn. A discarded
  unfinished encoder is not evidence of an invalid submitted command.
- `poll()` only advances the CPU completed frontier after every error-scope and
  fence result for the oldest operation succeeds. Any failure is sticky; its
  serial and every later serial remain uncertified. Retained resources cannot
  be recycled by pretending a failed submission finished successfully.

Callback cells are allocated with the store. Each armed cell temporarily keeps
its parent CPU state alive; the callback moves that reference before publishing
its final atomic status. The owner acquires that status before reading or
rearming the cell. Callback code touches no GPU object. This prevents userdata
use-after-free when an owner is abandoned with callbacks pending. It is a
bounded-storage design; these tests do not claim an allocation-fault or thread-
sanitizer proof of the WebGPU implementation itself.

Retirement prevents new declared uses but preserves previously reserved work.
CPU collection requires both real completed use and zero retained readers.
Only then does a checked queue operation zero the old descriptor identity and
return its spans. Queue ordering puts invalidation before any later replacement
upload; generation increments reject old GPU descriptors after slot reuse.

`close()` stops admission and waits for outstanding tickets, readers, all checked
operations and invalidation before releasing GPU references and reaching Closed.
Failure remains Failed. Destruction balances an abandoned open scope and releases
references, rather than calling BufferDestroy on buffers held by encoded work.
Normal owners must close/drain; whole-context abandonment requires discarding
dependent work and provenance too. Actual idle device-loss notification, recovery
and live-world context ownership still belong to backend integration.

`physics_authored_shapes.wgsl` bounds-checks header/descriptor ranges and shape
generation, then exposes the exact cell/face/BVH data. It includes root/principal-
body point, vector, orientation, COM velocity, inverse-inertia and impulse helpers.
Its geometry remains in canonical root space. Mass helpers require a valid
prepared view and caller-validated finite runtime poses/forces. The shader is
exercised by actual native and browser compute pipelines and readbacks, including
the rational off-center impulse from stage-a1. It is not yet called by live
collision, integration, rendering, query or attachment pipelines.

See [stage-c1 evidence](stage-c1/README.md). Typed backend admission, retained
compiled-revision mappings, all pose/force consumers, explicit unsupported CPU
handling, device-loss integration, live compound contacts and independent review
remain required. This checkpoint cannot certify SIM-02, SIM-03 or a gate.

## Stage-d1 persistent body layout and stored mass consumption

`GpuBodyShape` is now the shared, 16-byte-aligned, 64-byte C++ record for the
backend and direct primitive renderer. All nine WGSL `BodyShape` declarations
have the same four rows. The old dimensions/type, inverse-inertia/material and
material-coefficient rows retain offsets 0, 16 and 32. The new integer row at
offset 48 reserves shape index, shape generation, resource format and zero.
The CPU pool incarnation belongs to the owning world/atlas and is not encoded
in material bits or in the already full body generation/flag word. Static
layout assertions and actual rendering/culling tests cover the new stride.

This adds 16 bytes per allocated body slot. CPU compact rendering uploads
32 bytes of pose plus 64 bytes of shape per body. It does not add a storage
binding or expand the 128-byte command or 208-byte simulation uniform. The
authority content closure includes the new layout header, so this simulation
ABI change contributes to the generated authority identity on both platforms.

Primitive spawn computes its diagonal inertia once and explicitly zeros the
new reference; destruction clears it too. Material changes preserve the mass
and independent reference. The actual ballistic command, ballistic integration,
dynamic preparation and static-contact paths now consume the stored principal
inverse inertia. Previously they recomputed it from primitive dimensions and
would have discarded a prepared full tensor. The existing dynamic solver and
attachment paths already use the stored tensor. Kinematic targets continue to
set inverse mass and stored inverse inertia to zero.

The four new real-GPU cases execute the exact shipping ballistic shader:
off-center force plus velocity preparation against the independent rational
mass oracle; one-step COM-centered ballistic pose and world-root reconstruction;
primitive spawn/destruction clearing a poisoned reference; and a material
change preserving prepared inertia/reference bits. The force test uses an
authored principal tensor deliberately different from the dimensions-derived
cube tensor. Geometry/water interactions are explicitly disabled in these
isolated kernel fixtures; actual legacy consumers are tested separately.

No public caller can admit an authored body yet. Nonzero shape references in
these tests are controlled buffer fixtures. In particular, the existing
primitive geometry paths must not receive such a body and clamp its identity
to a primitive. The single-body rotation fixture validates COM/root mappings,
not general torque-free tumbling, compound contacts, gameplay or art quality.

The next integration must connect typed admission, compiled revision/provenance
retention and actual application queue ownership before enabling any authored
capability. Declare all shape uses before encoding and balance submit/discard
on the real queue; use completion callbacks for retirement. Review every
root-local force/attachment/query point, root pose and root/COM velocity
conversion, including sector-boundary normalization. The current integration
layout already uses eight storage bindings: do not assume a ninth is available
when adding the atlas to geometry consumers. Cached mass avoids an atlas
binding for these mass-only operations; geometry access still needs an explicit
layout design. Unsupported CPU backends must return a typed refusal with no
primitive fallback. Device loss/recovery and SIM-03's complete geometry consumer
matrix remain open. See [stage-d1 evidence](stage-d1/README.md).

## Stage-e1 backend ownership and facade access

`PhysicsWorld` now provides explicit `enableAuthoredShapeResources(limits)` and
borrowed `authoredShapeResources()` access. `IAuthoredShapeResources` exposes
the prepared shape, lifetime and checked-submission contract without exposing
the concrete GPU backend. It preserves the stage-c1 resource implementation;
the GPU-specific names remain source-compatible aliases. The facade header
forward-declares these types, keeping geometry/asset definitions out of unrelated
renderer and platform includes. Callers configuring resources include
`physics/authored_shape_resources.hpp` explicitly.

Uninitialized and moved-from worlds return `NotInitialized` with no interface.
The Jolt/Box3D backends return `Unsupported`, including an explicitly selected
CPU fallback. There is no conversion to a box, no inventory mutation and no
implicit backend switch. GPU allocation is opt-in: existing scenes allocate
no shape atlas until requested. An unsuccessful enable leaves the backend
without a resource interface. A successful enable publishes the owned interface
in Initializing; actual completion/scopes and polling determine Ready.
Reconfiguration returns `AlreadyConfigured` until the world is shut down.

Each backend allocation reserves a fresh process-local 64-bit identity from
an atomic monotonic sequence. Zero is invalid/exhausted. Reserving the last
identity permanently exhausts the sequence; it never wraps. Failed allocations
burn their identity because callbacks from partial GPU creation may survive.
Tests cover distinct identities across backend restart, not an executed 2^64
exhaustion experiment. These are runtime handles, not durable IDs or a security
boundary for serialized untrusted content. Production worlds use this owned
allocation path; low-level explicit-identity factories retain their documented
unique-identity caller requirement.

The backend owns the concrete store. Moving `PhysicsWorld` preserves its resource
object and existing borrows. Normal `update`, even with zero simulation delta,
polls resource completion; callers can also poll explicitly on the owner thread.
The allocated atlas is included in persistent GPU memory accounting, including
retiring resources. Its hard 32 MiB maximum makes its explicit conversion into
the existing wasm32 `size_t` telemetry field representable. Separate resource
statistics retain cumulative upload bytes, charged CPU storage, readers and
pending operations; cumulative uploads are not mislabeled as per-frame bytes.

Normal callers close/drain and release dependent encoders, commands and CPU
borrows before shutdown. Forced shutdown logs undrained abandonment, releases
the store and ends all interface borrows. It does not certify pending serials.
The existing store retains independent callback userdata and releases GPU
references without destroying a buffer still held by encoded work. A fresh
backend cannot reuse this identity. This is not an idle device-loss/recovery
implementation, nor permission to submit old world commands after shutdown.

The two new shared GPU cases use a real 32-body-capacity backend. One places
an authored atlas read and a primitive-body tick into the same checked queue
submission, immediately retires the shape, reads its real exterior data and
checks the primitive's completed position (2+1/60, 3, 4) with zero gravity and
damping. The other closes/restarts the backend and verifies that matching
slot/generation numbers in a new pool reject old handles while returning the
new shape's actual source labels. Additional native facade cases cover move,
drain, unsupported CPU backends and explicit fallback.

The full native authority closure now includes shape preparation/pooling, exact
geometry and the GPU store because the shared backend calls them. Bazel, CMake
and the sorted allowlist must agree; do not add the renderer/window/CPU-engine
implementation closure to the headless authority target to resolve a link.

Remaining work is still substantial: body admission and its complete geometry
capability, compiled revision/provenance retention, automatic declaration of
live body/render/query shape uses at the actual application submission boundary,
root/COM conversion at every consumer, fixed-tick submitted/completed frontiers,
idle device loss and recovery. A resource interface whose uploads pass is not
an authored-body capability. No application route yet calls this opt-in API.
The UI continues to run its existing primitive/static inspection scenes. Parent
SIM-02/SIM-03, visual approval and all dependent gates remain open.
The [stage-e1 validation record](stage-e1/README.md) passes all fourteen shared
GPU cases across both native build systems, strict undefined-behavior/float-cast
checks and actual hardware Chrome. Fourteen paired readbacks match exactly;
the native selection passes 131 cases and both application builds. Actual
Bazel/CMake authority closures and generated header equality pass. Review and
live authored-body activation remain separate from this resource checkpoint.

## Stage-f1 checked authored motion frames

The next integration boundary is `AuthoredBodyFrame`, an owned copy of a
validated shape's packed mass transform. Separate root-motion and body-motion
types prevent confusing authored-origin velocity with COM velocity. Conversions
use the actual quantized COM/principal quaternion that GPU consumers receive,
not a second decomposition of the original double tensor.

Positions remain sector plus local coordinate. Use double intermediates on
the small local shift and checked integer sector arithmetic; never reconstruct
a far absolute coordinate. Reject noncanonical input, invalid orientation,
nonfinite motion and world overflow without clamping or publishing partial
output. In particular, f32 rounding to local +128 must carry into the next
sector, or reject at INT32_MAX. Legacy primitive canonicalization is unchanged.

Root-origin velocity maps to COM velocity by adding `omega × world COM offset`;
the inverse subtracts it. Angular velocity remains in world axes. Root-local
force and attachment points use the packed inverse principal transform once.
The helper checks finite/representable motion and point results. Backend speed,
point/profile, tick, identity and capability admission remain caller work;
these math conversions alone are not an activation token or proof of collision.

Near-unit input quaternion squared norm must be within .001 of one. Accepted
orientations are normalized, with canonical sign and positive zeros on output;
zero, scaled, nonfinite and otherwise invalid values are rejected. Point maps
retain the packed shape quaternion's measured quantization, matching the GPU
helper convention. Native and actual browser tests must check asymmetric
motion/force values, nonzero world orientation, sector edges and limits, plus
actual shipping force/integration kernels using the converted inputs.

The [stage-f1 evidence](stage-f1/README.md) passes ten CPU frame cases and
sixteen shared GPU cases in optimized Bazel, CMake, strict undefined-behavior/
float-cast checks and actual hardware Chrome WebGPU. All sixteen paired GPU
readbacks match exactly on the reference adapter. Both applications build;
the helper is not yet wired into live authored-body admission or application
consumers. This supplies the checked conversion required by that integration,
not a completed body/geometry capability or a per-frame CPU readback path.
