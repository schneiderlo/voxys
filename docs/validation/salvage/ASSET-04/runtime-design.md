# ASSET-04 — bounded runtime bridge design

2026-09-07, `render_architecture`. **Design only.** No product source, build,
GPU execution or capture changed for this document. Root assigns implementation
ownership separately. This complements the [pontoon specification](design.md)
and supersedes old runtime line references in the LOOK-01 prework.

The deliverable is one verified, static pontoon fixture in the real native and
browser application. It includes three LODs, exact socket/axis overlays and a
drawn material diagnostic. It is not a sailing test, inventory operation,
streaming system or the full cove visual checkpoint.

## 1. Actual reusable path and missing boundaries

| Existing entry point | Current behavior and implication |
|---|---|
| `src/render/mesh_path.hpp:44` | `MeshDrawInstance` carries an asset index, logical mesh index and full model matrix. Its caller must evaluate nodes. |
| `src/render/mesh_path.cpp:629` | `loadMeshFromBytes` reads VMESH and uploads it. The local `VmeshData` then goes away; GPU assets retain no nodes. |
| `src/render/mesh_path.cpp:647` | Upload validates drawable geometry/materials and calculates indexed local mesh bounds. Shared mesh nodes can reuse these buffers. Indices become u32 regardless of file stride. |
| `src/render/mesh_path.cpp:1051` | Rendering validates affine nonsingular matrices, culls each transformed logical-mesh bound, expands submeshes and issues one indexed draw per submesh. This is CPU submission, not a GPU-driven part renderer. |
| `src/render/mesh_path.cpp:326` | Defaults allocate 256 draw-instance slots and cap expanded draws at 512. `maxInstances` is initial storage, not a complete CPU-list/residency limit. |
| `src/render/mesh_path.cpp:402` | Shutdown releases all assets. There is no per-asset removal or stable asset handle; indices are transient append positions. |
| `src/moto/vmesh_io.cpp:329` | Node references receive upper-bound checks; this does not prove finite TRS, parent ≥−1, acyclic traversal or proper rigid-profile semantics. |
| `src/moto/session.cpp:333` | Moto loading requires identity nodes. Preserve that legacy contract and its current indices/poses. |
| `src/app/application.cpp:4404`, `:4613` | MeshPath initialization/rendering is owned by the motorcycle route. Salvage needs its own caller, not a fabricated MotoSession. |
| `src/app/application.cpp:4703` | The blit linear-depth requirement currently recognizes meshes only when MotoSession exists; add the actual fixture predicate. Resize already refreshes the current mesh path's ray-depth binding at `:2453`; a separate path needs equivalent treatment. |

The shader already implements the relevant normal mathematics. Its cofactor
matrix and determinant sign at `shaders/mesh_path.wgsl:69` produce normalized
inverse-transpose normals. Tangents use the model linear transform; the fragment
stage orthogonalizes them against the normal and reconstructs the signed
bitangent (`:213`). Base/emissive textures use sRGB formats, normal/MR use linear
formats, and mip generation handles those spaces (`mesh_path.cpp:183`, `:247`).
Do not replace these paths merely because the new asset needs them.

## 2. Manifest admission before GPU allocation

Add a small pure CPU `CookedPartBundle` loader under `src/game/assets/`, separate
from MeshPath and the static fixture. It consumes a bounded byte provider so the
same code works with native files, WASM preloads and in-memory malformed tests.
The provider returns an owned snapshot; parsing, hashing and upload all consume
that same snapshot. Never hash a path and later reopen it to upload different
bytes. Native file admission must reject symlinks/nonregular inputs or use the
already established no-follow bounded snapshot behavior. Browser admission
uses packaged paths only; it does not fetch arbitrary manifest URLs.

Use the existing schema-1 `parseGameplaySidecar` and `PartCatalog` validation,
not a second physical-schema implementation. Its pure parser currently lives
at `tools/salvage_assets/gameplay_sidecar.*`; root should expose that one shared
implementation to both runtime and offline tools, moving it to a neutral source
library with compatibility includes if needed. The runtime must not shell out
to `gameplay_sidecar_tool`, TinyGLTF, Blender or Python.

Admission order, with a property-specific failure and unchanged active fixture:

1. A packaged fixture registry selects the exact part `ContentKey`, expected
   cook-manifest SHA-256 and bounded bundle/source roots. Validate the registry
   as data; do not derive content identity from a filename or MeshPath index.
   This pins the expected manifest so merely replacing a VMESH and its nearby
   manifest does not satisfy the original selection. It is content-integrity
   checking inside a trusted installed package, not a new signature system.
2. Bound and strictly parse manifest schema 1: duplicate/unknown keys, malformed
   hashes, noncanonical u64 strings, noninteger sizes, path traversal and excessive
   collections reject. Require exact `salvage-rigid-v1` profile/interface and the
   complete declared cooker/converter/validator identities. The packaged registry
   pins the expected manifest; runtime does not need copies of tool executables.
3. Read/hash the declared normalized `gameplay.json`; require exact length/digest.
   Parse it with the shared schema and catalog validator. For each stable LOD ID,
   require exactly one matching manifest entry and derived `lod-<id>.vmesh` name.
   Reject missing, duplicate or surplus bindings, mismatched source size/hash and
   a different part or visual content version. Compare the normalized result to
   the retained normalized bytes rather than silently accepting another schema.
4. Read/hash each exact cooked VMESH and call `readVmesh`. Then independently
   validate the runtime rigid-node/drawable contract and configured asset budgets
   below. Resolver success means the exact asset key maps to these admitted bytes,
   not merely that a file exists. Build the accepted catalog in a temporary owner.
5. Source GLB and tool hashes remain exact, bound provenance strings. Root's
   runtime packaging decision does not require shipping editable GLBs or tools:
   actual source/tool bytes were verified by the accepted offline cook. Runtime
   verifies the trusted manifest and source-binding consistency, and must not
   claim it rehashed absent GLB/tool bytes. Never invoke the importer at runtime.
6. Only after the whole bundle passes may the owner upload it. Publish catalog,
   LOD/basis mappings, instance records and GPU owner together after all uploads
   succeed. Never expose half the LODs or register v2 before its meshes exist.

The current standalone digest implementation is private to WRECKWATER content
code (`src/game/wreckwater_content_manifest.cpp`). Avoid making salvage assets
depend on that authority module. Use a small shared/tested SHA-256 utility or a
project-approved dependency, with known-answer and split-update tests. Root owns
the shared dependency/file move decision. Data-specific manifests remain data,
so a future accepted v3 selects its own immutable identity without C++/WGSL edits.

## 3. Rigid prefab evaluation and exact coordinate boundary

Proposed CPU record: one `RigidPrefab` per admitted cooked LOD, containing mesh
nodes, evaluated full local-to-asset matrices, logical mesh bounds, draw expansion
counts and a canonical render bound. No pointers into temporary VMESH buffers.
Retain stable LOD/visual keys outside the transient GPU asset index mapping.

Validate at most 256 nodes/meshes and 512 submeshes before traversal. Parent must
be −1 or a different valid node. Reject cycles, nonfinite values, skins/animations,
invalid mesh references, zero/nonpositive scales and nonunit quaternions. Evaluate
parents before children with a bounded visitation stack; node array order need
not be topological. Use `glm::quat(w,x,y,z)` explicitly for VMESH's XYZW storage.
For every node:

```text
local(node) = T(node.translation) * R(node.rotation) * S(node.scale)
asset(node) = asset(parent) * local(node)       # root parent is identity
draw(node)  = camera-relative assembly root
              * exact canonical part placement
              * recorded per-LOD glTF-to-canonical basis
              * asset(node)
```

The fixture root supplies its world-sector pose. The part placement is the
DATA-01 grid translation in metres and exact proper CubeRotation; it contains
no second assembly translation. For a standalone part its assembly root can be
identity. Convert ticks at 50/m once. Subtract the camera sector in integer/double
space before producing float render coordinates; reuse the bounded semantics of
`physics::worldPositionRelativeToSector` (`physics_types.cpp:104`). Do not convert
a large absolute float position and subtract another large float. Out-of-range
sector deltas reject/cull explicitly instead of wrapping.

Compute the static hierarchy in double precision, then validate all composed
matrices and transformed bounds before float conversion/submission. Positive
local scales can produce a sheared composed matrix under rotated parents:
**keep the full matrix**, do not decompose it back to a single TRS. Reject results
that are nonfinite, unrepresentable, singular or too ill-conditioned for the
float shader's cofactor/normal arithmetic. Match the renderer's determinant
check and test bounded extreme inputs; do not assume finite entries alone imply
finite cross products.

Each mesh-bearing node produces an instance, including two nodes referencing
one mesh. Empty transform-only nodes still affect descendants. Repeated part
placements reuse the admitted/uploaded LOD, never duplicate its material images.
Construct a complete bounded instance list before replacing the previous list.

Canonical sockets, collision, buoyancy and tool anchors receive only assembly
root and part placement; **never the render basis or glTF node transform**.
CubeRotation 12 belongs to exports that actually record it. Do not hardcode it
for every asset or reapply Blender's already-exported Y-up conversion.

Transform all eight corners of each indexed local mesh bound by the node and
LOD basis; union those into the canonical render bound. Include pegs up to
Y .66 m, not only the .96 m body footprint. Derive a conservative part LOD bound
from the union across LODs so choosing an LOD cannot itself change the selection
measure. Keep physics occupancy bounds separately exact on the lattice.

## 4. GPU owner, budgets and teardown

For ASSET-04 use a fixture-owned MeshPath instance with its own short asset list.
Leave Application's existing motorcycle owner and asset numbering unchanged.
This makes failed multi-LOD admission transactional: build a candidate owner,
upload all admitted assets, then swap at a frame boundary. A failed candidate
can release its own handles without removing any active legacy assets.

Do not add general per-asset eviction or a global streaming cache here. Bound the
fixture to 32 logical part placements, 256 mesh-node instances and 512 expanded
draws, checked before calling `addInstance`; diagnostic/overlay draws count too.
The three pontoon LODs must each satisfy the author's triangle/vertex/material/map
limits. Count actual GPU geometry as `72 * vertices + 4 * indices`, material
storage as `64 * materials`, and texture residency as the full RGBA8 mip sum per
uploaded material slot. Include retained instance/uniform/fallback resources
separately. Deduplicate repeated references by admitted visual key plus exact
digest; do not imply cross-LOD or unrelated-material image deduplication.

Require the full three-LOD pontoon asset set to remain under its specified 8 MiB
requested geometry/texture ceiling; report diagnostics separately with a bounded
fixture-wide ceiling chosen from actual authored counts. Cap cumulative input
snapshots/parsed bytes before allocation as well. Offline ASSET-03's 256 MiB
ceiling is not an acceptable per-pontoon runtime budget. Query WebGPU device limits
before upload. Requested storage, CPU decoded bytes, mip-generation temporaries,
queue upload staging and opaque driver overhead are separate observations.

Reset restores fixture transforms/camera/LOD controls while retaining the
immutable resident assets. Leave/reload queues a frame-boundary transition:
stop submitting old instances, clear their transient mappings, and retain old
resources until their last command submission is safe to retire. Existing
`MeshPath::releaseAsset` destroys textures, so do not tear down an owner while an
encoder that references it has not yet submitted. Implement a bounded retirement
owner with a portable queue-completion notification, or use release-only handle
retirement with a verified WebGPU retained-reference contract as already used by
`Application::createBenchmarkTarget(:3603)`; do not substitute a fixed frame delay.
Publication must not create an unbounded old-owner queue. Candidate never drawn,
failed initialization and device-loss/application shutdown need explicit branches
that cannot wait forever for a submission that never occurred. Async callbacks
capture stable state/generations rather than a freed fixture `this` pointer.

Allocation/upload failure, including asynchronous GPU validation failure, keeps
the previous admitted fixture. Account for temporary candidate+active resources
when setting the admission ceiling. Report counters for active assets, unique
GPU uploads, requested bytes, pending retirement and submitted draws so repeated
Reset/Leave/re-entry can prove bounded ownership.

## 5. Application fixture and material draw proof

Add an explicit opt-in asset-fixture selection in salvage configuration/routes;
keep existing `salvage` preview and default/LEGO/RIDGEBREAK behavior unchanged.
Root owns exact config/entrypoint/build registration. The fixture owns static
visual instances and tool overlays, not physics bodies or a GameSession. Its
dry stand is placed visibly above the shore, with no waterline acceptance claim.
Reuse the existing camera, sun/sky, fog/exposure, negotiated color format,
hardware object depth and ray-terrain occlusion. Wire init, frame draw, depth
requirement, resize binding refresh, Reset/Leave and partial-init shutdown.

The first fixture includes the designed pontoon pair, prototype cross beams,
separate v2-on-v2 nesting pair, and asymmetric axis/material diagnostic. Validate
the exact socket connections through BuildModel. Prototype beam visuals are
labeled; metadata fit is not a claim they contain authored bottom wells. Toggle
overlays off to inspect actual pontoon peg/well engagement. No physics proxy
render suppression is needed when this dry stand creates no proxy bodies; that
integration remains required when authored meshes replace BOOT-05 bodies later.

Use the current presented mesh shader for this dry test. The water/opaque HDR
ordering and object-shadow work remain LOOK-01; do not silently treat the current
presented color as linear water input or patch the landscape during this task.
Strict positive determinants/proper rotations avoid the known reflected-instance
front-facing mismatch. Check real single-sided winding by orbiting all sides;
the current shader rejects back faces per material rather than using a separate
hardware-culling pipeline.

The existing `tests/test_mesh_path.cpp:47` uploads a synthetic textured asset as
index 3 but its submitted instances never select index 3. Add a dedicated draw
test and fixture diagnostic that visibly samples the intended textures. Read
actual color pixels for quadrant orientation and normal-light direction changes,
using tolerant regional comparisons under declared exposure instead of exact
tonemapped palette bytes. Test asymmetric nonuniform parent/child transforms
with a diagonal normal, authored tangent and signed normal map. This proves the
existing inverse-transpose/tangent path through rasterization, not just CPU math.

Select LOD from projected height of the shared conservative bound and actual
physical viewport/FOV. Intersecting the near plane selects the highest LOD;
behind-camera/culled objects do not produce an invalid divide. Fixed LOD controls
are for inspection. Record chosen LOD and threshold crossings; add small explicit
hysteresis if needed, shared by native/browser, not separate distance guesses.

## 6. Implementation split and acceptance

Proposed shared CPU API, awaiting root approval before implementation:

```text
game/assets/rigid_prefab.hpp/.cpp
prepareRigidPrefab(VmeshData, CubeRotation renderToCanonical,
                   RigidPrefabLimits, output RigidPrefab, output error) -> bool
```

Inputs are const references. Failure leaves the existing output unchanged.
`RigidPrefab` owns mesh-bearing node records (logical mesh index and double
node-to-asset matrix), recorded basis, indexed local/canonical bounds and actual
geometry/draw/mip counts. It owns no GPU handles and does not parse the sidecar.
The root-owned immutable bundle may retain both decoded VMESH and this prepared
record per LOD. The bundle loader calls this CPU validation boundary; game asset
admission does not depend on the renderer or duplicate its graph logic.

A separate placement helper takes a camera-relative root double matrix plus
an exact `GridTransform` and produces a bounded list of float node draw records.
The application owns world-sector subtraction, catalog placement and LOD choice;
the helper owns the exact part/basis/node multiplication and final numeric checks.
MeshPath receives the resulting records. Add a narrow public
`loadMeshData(const VmeshData&)` to upload the already admitted snapshot, avoiding
a serialize/reparse cycle. It preserves the existing upload validator and legacy
byte/path APIs. A fixture-owned candidate MeshPath provides the complete
multi-LOD transaction; existing motorcycle asset indices remain unchanged.

| Work package | Bounded dependencies and tests |
|---|---|
| Root: CPU bundle admission | Shared parser relocation, SHA-256, registry/manifest admission; malformed manifest, exact hash/version/LOD mismatch and owned-file-snapshot tests. Calls the shared prefab validator. |
| `render_architecture`: CPU prefab + bounded mesh upload | Graph/transform/bounds/geometry-budget tests, placement helper, narrow MeshPath data upload/ownership changes and actual material draw tests. Product file claims remain subject to root approval. |
| Root: Application fixture | Accepted bundle and actual authored candidates; route/camera/LOD integration, candidate publication, lifecycle/failure injection and runtime evidence scheduling. |
| Blender authoring | Separate author owns the parameter recipe, three exports/maps, canonical sidecar, physical calculations, provenance and offline previews described in `design.md`. No concurrent renderer edits. |
| Root integration | Shared BUILD/CMake/preloads/Pages/package checker, route controls and validation scheduling; same exact admitted bundle in both application packages. |

CPU cases must include reversed node order, repeated shared mesh, empty parent,
cycle, parent −2, nonunit quaternion, singular/overflowing composed transforms,
all 24 part rotations, negative placement and both sides of a sector boundary.
Compare independent transformed marker points and exact socket positions; a
round trip alone is insufficient. Tampered bytes and failed replacement keep
the previous content key, instance list and GPU owner. Repeat selection of v1
and v2 explicitly; no automatic migration or default-key replacement.

After root coordinates the GPU window, capture actual native and browser fixture
images, socket close-up, all-side views, material-light variation and a short
camera/LOD movement. Match physical dimensions, camera, FOV, exposure, light and
LOD; record logical viewport and DPR independently. Retain exact source, package,
manifest and VMESH hashes. Reset/reload/Leave/re-entry and legacy RIDGEBREAK startup
must still work. No independent graphics evidence was created during this design.

ASSET-04 stays pending until the generated asset, physical metadata, real draws,
package/lifecycle checks and independent technical review all pass. A correct
importer or Blender preview alone does not establish improved in-engine art.
