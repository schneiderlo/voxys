# LOOK-01 technical prework — asset-to-scene minimum

2026-09-07, `render_architecture`. **Investigation only, excluded from G00.** No
renderer/product changes, builds, GPU runs or checklist updates were made. One
small offline experiment used the already-built converter and temporary files.
This is not ASSET-03/04 or LOOK-01 acceptance. The owner rejected the raw BOOT-05
fixture's appearance; retain that feedback and the original landscape reference.

## What can be reused

- `tools/gltf_vmesh_tool.cpp:1096` loads glTF/GLB and builds materials, meshes,
  skins, nodes and animation records; CLI at `:1143` is
  `gltf_vmesh_tool input.glb output.vmesh`. `src/moto/vmesh.hpp:34` defines VMESH
  v1: 72-byte vertices, per-submesh mesh/material indices, four RGBA8 texture
  slots, TRS nodes, skin/animation records. `vmesh_io.cpp:321` validates references;
  serialization does not mean the runtime evaluates every stored feature.
- `src/render/mesh_path.cpp:650` uploads geometry/materials and local mesh bounds.
  `mesh_path.hpp:44` accepts `(assetIndex, meshIndex, modelMatrix, tint)` instances.
  `mesh_path.cpp:1115` culls transformed bounds; `:1127` expands each visible
  instance into one draw per submesh; `:1157` sorts opaque/material and blended
  draws, `:1178` uploads the CPU draw list each frame. Defaults are 256 initial
  instances and a hard 512 expanded-draw limit (`mesh_path.hpp:38`); this is enough
  for a bounded authored scene after counting its actual submeshes, not a future
  large assembly renderer. No GPU-driven clustered parts are required for LOOK-01.
- `shaders/mesh_path.wgsl:69` already applies a cofactor normal transform and
  tangent handedness; `:214` supports tangent normal maps. `:242` onward provides
  metallic/roughness, shared sun, roughness-dependent sky lookup, emissive,
  fog/exposure and presentation. Base/emissive are uploaded as sRGB and normal/MR
  as linear; mip generation exists (`mesh_path.cpp:183`, `:247`). Do not replace
  the working PBR path with the flat primitive fixture shading.

## ASSET-03: fix or explicitly exclude these cases

1. **Matrix rotations are currently reversed.** `gltf_vmesh_tool.cpp:287` treats
   column vectors as matrix rows during quaternion extraction. A column-major
   +90° Z matrix `[0,1,0,0,-1,0,0,0,0,0,1,0,0,0,0,1]` should yield quaternion
   `(0,0,+0.707107,+0.707107)`. The existing converter produced
   `(0,0,-0.707106769,+0.707106769)` with exit 0. This was observed using binary
   SHA-256 `79785b8fdde4c1488b10b5c2b312be9e855aa613fb988e36d3fe89c0db3627cb`;
   importer source has no working-tree delta. Fix and test matrix/TRS equivalence,
   or reject node matrices in the explicit initial profile. Also reject
   perspective/shear/degenerate matrices rather than approximating them as TRS.
2. **Validate attribute counts before reading.** `convertPrimitive` selects known
   attributes (`:568`) but iterates them using POSITION.count (`:605`, `:645`).
   `readFloatComponent` (`:123`) and `readRawUInt` (`:169`) do unchecked element
   addressing. Require matching NORMAL/TANGENT/UV/JOINT/WEIGHT counts first;
   individual accessor byte-range validation does not establish this relation.
3. Preserve existing rejection of sparse accessors (`:82`), non-triangles (`:545`),
   out-of-range indices (`:640`) and unsupported selected UV sets (`:437`). Add
   explicit allowlists for required extensions, unknown attributes/extra UVs or
   influences, morph targets, vertex colors and texture transforms. The converter
   currently only searches attributes it recognizes; it does not establish that
   omitted appearance is optional. Require usable normals for authored parts:
   missing normals currently silently become `(0,1,0)` (`:658`, tested as current
   behavior in `tests/test_gltf_vmesh_tool.cpp:289`). Export tangents for normal maps.
4. Start with one selected, acyclic scene and rigid mesh nodes, exact TRS lengths,
   finite nonzero scales/unit quaternions, supported embedded PNG/JPEG textures,
   bounded material/image/node counts and decoded bytes. `buildNodes(:902)` walks
   every scene, stores all nodes and has no active-scene field in VMESH; shared
   nodes across scenes and unreferenced nodes need a defined policy. The reader
   checks parent range (`vmesh_io.cpp:330`), not an evaluable acyclic hierarchy.
5. Material profile must match actual output. The four slots are base, normal,
   metallic/roughness and emissive. Occlusion strength is stored but an occlusion
   texture is neither cooked nor used by the shader. glTF sampler choices are
   replaced by linear/repeat/4× anisotropy (`mesh_path.cpp:354`). Either implement
   declared appearance (e.g. explicit ORM packing with gated AO) or reject it;
   don't promise AO, transmission, clearcoat or arbitrary sampler fidelity.
   Restrict the first opaque kit to positive/applied scale: negative determinant
   normals are handled, but fragment back-face rejection still uses unadjusted
   `front_facing` (`mesh_path.wgsl:179`). Supporting mirrored instances also needs
   correct face orientation. Skin/animation fields are stored, not consumed by
   the mesh shader; a rigid initial profile is sufficient for these props.

## ASSET-04: the missing runtime bridge

`MeshPath::uploadMesh` discards node hierarchy information. `Application::initMoto`
(`application.cpp:4361`) alone initializes the path; `renderMoto(:4576)` additionally
requires a live MotoSession. The existing Moto loader **rejects non-identity root
nodes** (`src/moto/session.cpp:323`), so it is not a general prefab loader.
The authoring probe deliberately retains object translations: simply drawing all
logical meshes with identity matrices collapses its markers and real kit pieces.

Add a small rigid-prefab layer for salvage that retains validated node/name data,
evaluates `parentWorld * T * R * S` once for static assets, and submits every mesh
node, including multiple nodes referencing one logical mesh. Resolve stable
content keys to transient MeshPath indices; do not persist those array indices.
A scene instance should use:

```text
camera-sector-relative part placement * part rotation
    * recorded glTF-to-canonical basis * evaluated glTF node transform
```

`construction_types.hpp:16` defines 50 ticks/metre (one current world stud is one
metre, not physical LEGO millimetres). The ASSET-01 Blender exporter already maps
`(x,y,z) -> (x,z,-y)`; **this probe's** remaining recorded basis is
`diag(-1,1,-1)` (proper 180° Y rotation, CubeRotation 12). Future assets use their
per-LOD `lods[].source.to_canonical_rotation` / `renderToCanonical`; do not hardcode
the probe's bridge globally. Apply that bridge **once** above the evaluated glTF
hierarchy. Canonical sidecar sockets/collision/buoyancy/tool anchors receive only
part placement and part rotation, **never the GLB basis**. Do not reapply the raw
Blender conversion. Current draft `gameplay_sidecar.hpp:43` leaves VMESH vertices
and nodes unchanged. Runtime binding must verify the cook manifest's exact VMESH
output digest and normalized sidecar/converter identity as well as source GLB
SHA-256; the source hash alone does not validate cooked bytes. `lego_gameplay`
confirmed this multiplication order and schema contract after G00 review; this
runtime adapter is still planned, not implemented.

Make salvage instantiate/load/render MeshPath independently of MotoSession,
including resize binding refresh (`application.cpp:2406`) and the linear-depth
requirement (`:4662`). Separate owned collision proxies from visual instances:
keep walkable bounded boxes where useful, but stop drawing their raw box surfaces
when authored meshes replace them. `PrimitivePath` currently has no per-owner
visibility suppression API: add a narrow generation-safe owned-body render filter
or an equivalent explicit collision-only contract, keeping unowned visuals and
physical collision intact. Preserve Reset/Leave ownership and resource cleanup. Add cooked assets/sidecars to both native staging and WASM preloads in
BUILD and root CMake, then the package/Pages manifest; files merely present under
`data/salvage/` are not yet shipped automatically.

## LOOK-01: unavoidable visible integration work

**Water composition is a real blocker for a wet pontoon, not a material tweak.**
The current order is physics → terrain/water blit → object depth clear → primitives
→ Moto meshes (`application.cpp:1888`, `:1916`, `:1922`, `:2079`). The cached water
pass reads static terrain HDR/depth (`blit_path.cpp:2345`) and writes final
presented color plus water depth (`water_clipmap.wgsl:501`, `:731`). Mesh fragments
then discard behind that final depth (`mesh_path.wgsl:197`), so submerged hull
pixels cannot contribute to refraction; the water also has no rendered boat to
occlude/refract. Drawing the vessel higher only conceals the defect.

The bounded fix can reuse the existing RGBA16F static background, without a new
large-world architecture:

1. Prepare the terrain/sky cache; retain it as immutable scene-independent input.
2. Populate separate per-frame opaque HDR color and linear depth from that cache.
3. Draw authored opaque meshes into those targets before water. Add a linear HDR
   mesh output variant and linear-distance attachment; keep the shared hardware
   object depth for object/object tests and a separate terrain-depth sample for
   terrain rejection. Current mesh output is already tone-mapped/sRGB (`:297`),
   which must not be fed into water as if it were linear radiance.
4. Composite the existing displaced water against the **combined** opaque inputs
   into separate final color/depth. Do not bind a texture for sampling while it
   is an attachment; the existing cached bind-group comment (`blit_path.cpp:2331`)
   demonstrates this constraint. Do not bake mutable props into static history.
5. Keep transparent props/particles/UI in explicit later stages. An opaque open
   skiff can omit glass at this checkpoint; full transparency/dynamic composition
   remains REND-01–04. Two new opaque textures cost `12 * width * height` bytes
   before other resources (about 24.9 MB at 1920×1080); reuse existing hardware
   depth where valid and measure the actual revised allocation.

Mesh sun lighting currently has **no cast/received object shadow input**
(`mesh_path.wgsl:25`, `:260`), while terrain/water use heightfield shadows. Geometry,
bevels and real normals will help immediately; they do not solve grounding.
Review contact/cast shadows under the dock, generator and craft. Implement bounded
static shadow/occlusion support if those objects still appear detached; don't
bake directional sunlight into their base color or claim the full later dynamic
shadow contract is complete. Preserve the original terrain/water appearance at
matched settings while making the above integration changes.

## Small acceptance set before composing the full kit

- Expand `tests/test_gltf_vmesh_tool.cpp` (currently 19 cases) with malformed-count,
  unsupported-feature and matrix/TRS/asymmetric hierarchy cases. Test the actual
  selected-scene policy and limits, including zero/negative/nonfinite transforms.
- Test the prefab bridge on a parent rotation, child translation, two instances of
  one mesh, asymmetric basis markers and negative camera sectors. Compare visible
  bounds/socket centres to canonical sidecar values, not only binary round trips.
- Fix the test gap at `tests/test_mesh_path.cpp:47`: its synthetic textured asset
  is uploaded as asset 3 but **never instanced**; the submitted 77 draws (`:179`)
  are 52 bike + 24 rider + one track. Render the synthetic material and read actual
  pixels under changed light direction/roughness; verify normal-map orientation,
  sRGB/linear handling, winding and alpha policy on native and browser.
- Validate one pontoon end to end at metre scale and socket fit, then compose a
  recognizable two-pontoon skiff, narrow dock and generator from the authored kit.
  Use real bevels/fasteners/panel joins and a controlled material palette. Frame
  foreground, craft silhouette and water/horizon; no huge box blocking the view.
- Capture a short camera move across the waterline, silhouettes and contact areas;
  include reset/removal with a stationary camera to catch stale opaque cache.
  Compare actual native/browser images at declared matching settings against
  `BOOT-05/browser-legacy/default.png` and the original art direction. Distinguish
  stationary preview physics from sailing. An offline Blender render cannot pass.

No LOOK-01 checkbox should be marked from this handoff. The two importer findings
are pre-existing; no repair is included in the frozen G00 checkpoint.
