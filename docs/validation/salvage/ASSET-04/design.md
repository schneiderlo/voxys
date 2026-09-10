# ASSET-04 — first authored pontoon specification

Design preparation by `lego_gameplay`, 2026-09-07. **No pontoon asset has been
generated or accepted by this document.** ASSET-03 must pass before this work
can claim pipeline acceptance. This file specifies the next asset and its
technical fixture; it does not record a successful boat simulation or final art
approval. Root approved the version-2 policy below during this preparation.

## 1. What the player should recognize

Build one substantial, manufactured flotation part: a cream plastic hull with
tapered ends, broad flat attachment surfaces, distinct assembly seams and a few
teal/coral safety details. Two copies should immediately read as the flotation
of a small salvage skiff. It should look useful, repairable and assembled from
parts at the normal play camera distance.

Use the [written art direction](../../../../GAME_IMPLEMENTATION_TODO.md) and
[concept with its original prompt](../../../concepts/build-explore-salvage-prompt.md).
The concept's rounded, segmented floats and clear color masses are references.
They are not exact dimensions, an approved mesh, or a render acceptance image.
Avoid a single beveled rectangular box, dense decorative machinery, tiny noisy
scratches, branded studs/logos and painted directional lighting. Keep useful
bevels and joins visible from approximately 4–12 m; inspect that in engine.

The owner rejected the BOOT-05 placeholder scene visually. ASSET-04 therefore
aims for a convincing first component even though its formal gate permits
prototype art. An isolated beautiful Blender render cannot pass LOOK-01 or
replace the later complete skiff/cove visual review.

## 2. Preserve the prototype; publish a new definition

The existing catalog really defines a sideways pontoon. It is not just an
incorrect comment. See [the starter definition](../../../../src/game/construction/part_catalog.cpp)
and [content types](../../../../src/game/construction/part_catalog.hpp).

| Property | Existing Pontoon v1 | Proposed authored Pontoon v2 |
|---|---|---|
| Definition namespace | `766f7879732d73616c766167652d7631` | Same namespace |
| Definition counter | Decimal string `"3"` | Same counter |
| Content version | `1` | `2` |
| Long axis | Local X | Canonical −Z forward |
| Body full dimensions | X 4 m, Y .96 m, Z 1 m | X 1 m, Y .96 m, Z 4 m |
| Body footprint half-extents | `{100,24,25}` ticks | `{25,24,100}` ticks |
| Visual | `PrototypeBoxVisual` | Three exact cooked mesh LOD identities |
| Collision / buoyancy | One full box, 3.84 m³ | Nine authored boxes, 3.538688 m³ |
| Dry mass | 120 kg | Initial design input: 120 kg |
| Structural sockets | Central top/bottom, IDs 1/2 | Central and two longitudinal pairs |

Keep the existing v1 definition and `starterPartKey(StarterPart::Pontoon)`
behavior unchanged. `PartCatalog` supports different versions of the same
durable definition ID; its duplicate check is on the entire `ContentKey`.
Load v2 explicitly from an authored catalog/fixture manifest. Do not make an
existing v1 lookup return v2, or rewrite old saves and blueprints during asset
loading. Missing exact content must remain an explicit error.

Changing even the visual asset in an already published definition changes its
content. Reserve v2 while producing isolated candidate bundles; bind its final
immutable definition to the accepted source/cook identities at publication.
Never publish two different accepted bundles under the same content key.
Subsequent accepted parameter, socket, mass, material or visual changes need a
new version. A G02 designer parameter-change demonstration can produce v3 in
a new directory and select it through data, without editing C++ or WGSL.

For reference, the **geometric** old-to-new reframe is proper CubeRotation 21:

```text
C = [ 0  0  1 ]       p_new = C * p_old = (z_old, y_old, -x_old)
    [ 0  1  0 ]       C^-1 is CubeRotation 9
    [-1  0  0 ]       R_new = R_old * C^-1; translation unchanged
```

This would preserve world geometry for a pure reframe. It would also require
`socket_new = C * socket_old` and `I_new = C * I_old * C^T`. The old box inertia
would become diagonal `{169.216,170,19.216}` kg·m². **It is not a v1→v2 migration:**
v2 changes cap shape, socket key orientation, volume, inertia and drag intent.
Its top socket keys deliberately align with canonical +X so the existing
crosswise beam sockets fit. A future migration must map every endpoint and
revalidate the complete assembly, inventory and provenance transactionally;
some old assemblies may require a player repair. ASSET-04 implements no such
migration and never treats design duplication as physical inventory creation.

## 3. Shape, scale and frame

The body origin is its symmetric center, not its bottom or a Blender object
bounding-box corner. Canonical body bounds are `[-.50,-.48,-2.00]` to
`[.50,.48,2.00]` m. Front is −Z. The render bound additionally includes the
top mounting pegs up to Y `.66` m; render culling must not use only the body
footprint and clip those pegs.

Start with an eight-sided chamfered rectangular cross-section swept through
these symmetric stations. Coordinates below are canonical metres; interpolate
between stations. The chamfer removes an isosceles right triangle of leg `c`
from each corner of the rectangle.

| Absolute Z | Half-width X | Half-height Y | Corner chamfer c |
|---:|---:|---:|---:|
| 0 to 1.40 | .50 | .48 | .08 |
| 1.80 | .42 | .40 | .04 |
| 2.00 | .30 | .28 | .02 |

Give the center shell a controlled molded finish. Add shallow, recessed
circumferential joins near Z `±.70` m and cap interfaces near `±1.40` m.
Use narrow teal inset bands and a coral bow marking to make orientation
legible without making color the only cue. A small asymmetric molded notch
or raised shape at the front reinforces the direction. Keep these decorations
inside the body envelope except the declared attachment pegs; avoid floating
decals and coplanar faces.

Author shell bevels around 1–3 cm where they change the highlight, with enough
geometry to survive grazing views. Broad cap chamfers define the silhouette;
microtexture must not replace that geometry. Make bottom socket wells visible
when the part is rotated for inspection. Decorative joins do not create
independently moving pieces or additional physical bodies.

Use single-sided opaque surfaces with outward winding. Socket-well walls face
into the accessible well. Do not use `doubleSided` to hide flipped triangles,
reflected transforms or an incorrect canonical basis. The visual shell may
contain separate decorative surfaces, but the authored enclosure used for a
volume comparison must be closed and consistently oriented.

### Blender and glTF boundary

Author at unit scale in metres, Blender +Z up and −Y forward. Apply scales and
modifiers before export; keep positive unit scale on production mesh nodes.
The existing Y-up exporter maps `(x,y,z)` to `(x,z,-y)`. With the approved
remaining CubeRotation 12, `diag(-1,1,-1)`, the total mapping is:

```text
Blender (x,y,z) -> canonical (-x,z,y)
Blender -Y -> canonical -Z forward
Blender +Z -> canonical +Y up
Blender -X -> canonical +X right
```

Place the asymmetric **right-side** marker at Blender −X with this convention.
Do not label Blender +X as canonical right. The raw Blender conversion is
already in the GLB and must not be applied again. Each LOD records its own
checked `source.to_canonical_rotation`; use 12 only if its actual export agrees.

VMESH vertices and node transforms remain in the exported glTF frame. The
runtime fixture must use:

```text
camera-sector-relative part translation
    * exact part rotation * recorded LOD basis * evaluated glTF node hierarchy
```

Canonical sockets, collision boxes, buoyancy and tool anchors receive only
the part placement, with ticks converted to metres once. They do not receive
the GLB basis. Preserve an independent asymmetric axis fixture so symmetry
cannot conceal an incorrect transform.

## 4. Socket fit and authored physical metadata

All frames use the .02 m DATA-01 lattice. The default v2 permits all 24 proper
rotations. Its body occupancy, collision and flotation are simplified metadata,
not triangle-mesh physics and not decorative-stud contacts.

### Socket recipe

All six sockets are structural family, profile 1, capacity 1, with supported
weld connections. Top sockets use rotation 0 and `Plug`; bottom sockets use
rotation 2 and `Receptacle`. Thus socket-local +Y points outward and local +X
is the shared key direction. IDs are stable decimal-string u64 values.

| Socket ID | Meaning | Translation in ticks |
|---|---|---|
| `1` | Center top | `{0,24,0}` |
| `2` | Center bottom | `{0,-24,0}` |
| `101` | Forward top | `{0,24,-50}` |
| `102` | Forward bottom | `{0,-24,-50}` |
| `103` | Aft top | `{0,24,50}` |
| `104` | Aft bottom | `{0,-24,50}` |

Use the existing socket-local clearance convention exactly: plug
`[-15,0,-15]..[15,9,15]`, receptacle `[-15,-9,-15]..[15,0,15]` ticks.
These are insertion reservations, not extra occupied solids or buoyancy.
Their world relationship changes with the bottom rotation; do not guess it
from a drawing. DATA-03's exact normal/key and engaged-clearance rules remain
the authority. Every enabled connection reserves its endpoint capacity.

The visible top peg fits inside a .48 m wide keyed profile and is .18 m high.
Use a clear +X key feature, such as a D-shaped flat, rather than relying on a
perfectly circular stud to explain orientation. The mating bottom well has
approximately .01 m radial clearance and .02 m spare depth. Those visible
manufacturing gaps do not change the exact socket origins or key alignment.
Give wells real depth and correct normals; do not paint a black circle as the
only close-view recess.

A top/bottom weld of two full-height parts advances the part centers by
`.96` m / `48` ticks. Peg insertion must not add `.18` m to that spacing.
Loose collision stacking remains a separate later simulation behavior.

### Nine simple physical regions

Use the following non-overlapping boxes, each at rotation 0. The same stable
IDs may be used in the separate solid, collision and buoyancy collections;
IDs are scoped by definition and collection. Each buoyancy record is
`sealed_compartment`. These are numerical integration regions for this part;
the metadata does not implement independently floodable chambers.

| ID | Center Z ticks; X=Y=0 | Half-extents `{X,Y,Z}` ticks |
|---|---:|---|
| `1` | 0 | `{25,24,70}` |
| `2` | -75 | `{24,23,5}` |
| `3` | -85 | `{22,21,5}` |
| `4` | -92 | `{20,19,2}` |
| `5` | -97 | `{17,16,3}` |
| `6` | 75 | `{24,23,5}` |
| `7` | 85 | `{22,21,5}` |
| `8` | 92 | `{20,19,2}` |
| `9` | 97 | `{17,16,3}` |

Their faces meet exactly without positive-volume overlap. Their total volume
is **3.538688 m³**. The undecorated eight-sided swept shell above encloses
approximately **3.492586667 m³**, so this initial proxy volume is about 1.32%
larger before the mounting details are included. These numbers are analytic
design calculations, not measured export results.

Author an explicit closed enclosure for the final volume check, accounting
for wells and pegs. Require the reported proxy-volume error to remain within
5% of that chosen enclosure and surface-distance error within .08 m on the
main collision shell, excluding the deliberately non-colliding pegs and mating
well regions. The box proxies intentionally bridge the wells; they are
approximate contact geometry, not exact visual skin collision. Enclosure volume
still includes the actual pegs and wells. Validate peg/well containment across
all LOD combinations separately, including axial spare depth. Root approved
this metric clarification during authoring. Measure
the actual generated shape; these thresholds have not passed yet. If the
shape misses them, refine the recipe instead of silently declaring the visual
mesh itself to be the collision shape. No decorative joins contribute another
copy of the sealed volume.

### Initial mass and module inputs

Use 120 kg dry mass and COM `{0,0,0}` m as explicit tuning inputs. For this first
recipe, distribute dry mass uniformly over the nine proxy regions, in
proportion to their volume. This is an authored lumped approximation to the
shell and fittings, not a claim that hollow plastic has uniform real density.
Compute each box's inertia and combine it with the parallel-axis formula.
The default diagonal tensor about COM is approximately:

```text
Ixx = 148.778437097591 kg m²
Iyy = 149.539266439991 kg m²
Izz =  18.190337842726 kg m²
Ixy = Ixz = Iyz = 0
```

The generator calculates full-precision values from parameters; it must not
copy these rounded display numbers into all size variants. Run the existing
C++ `PartCatalog` checks on the actual sidecar, including finite positive mass,
physical tensor inequalities and bounded COM/second moments. Do not duplicate
those invariants in a divergent Python-only acceptance path.

Initial `FlotationModule.dragCoefficients` are `{.9,1.2,.4}` in the **new**
X/Y/Z frame. Lower longitudinal drag is a design intent for later tuning;
these values are not a rotated equivalent of the old prototype. Retain the
initial strength ceilings of 30,000 N tension, 24,000 N shear, 18,000 N·m
bending and 12,000 N·m torsion. Socket strengths do not exceed those limits.
Initial cost/yield are 24/16 salvage-material units and zero special machinery;
starter-loan provenance still overrides physical recovery yield to zero.

Full-submersion volume is not usable payload, stability, a draft measurement,
or proof that the first skiff works. Actual displacement, waterline, roll,
drag, damage/flooding and carrying capacity remain SIM-01–09/G03 tests.

Tool-anchor defaults are stable IDs `1` at center `{0,0,0}`, `2` at bow
`{0,0,-100}` and `3` at central top socket `{0,24,0}`, all rotation 0, with
distinct localization/name keys. They are authoring/tool reference frames;
no character interaction or new physical authority is implied.

## 5. Material and LOD budgets

Use one shared opaque PBR material per LOD with one packed UV0 atlas. Join
production render pieces into one logical mesh after preserving a readable
assembly shape. This avoids paying a draw/material texture set for each seam
or peg. Authoring collections may remain separate in the `.blend`.

Target about 70% warm cream shell, 20% muted teal bands and underside details,
and 10% coral/slate hardware accents by visible area. These are composition
guides, not a rule to stripe every surface. Suggested **sRGB reference colors**
are cream `#D9C9A2`, teal `#2A6767`, coral `#CF6548`, slate `#354852`.
Convert them to linear values when setting linear PBR factors. Do not multiply
a colored atlas by another colored factor and unintentionally tint it twice.

Plastic is nonmetallic, with roughness approximately .40–.58. Recessed dark
rubber-like details can be .65–.80; the few exposed steel pins may use metallic
1 with .32–.45 roughness. Pack metallic in B and roughness in G; unused R is
not a functioning AO slot. Base color is sRGB, normal/MR maps are linear.
Use glTF tangent-space normals and actually exported tangents; establish the
normal-map orientation with the diagnostic fixture before baking wear.

| Resource | LOD 0 | LOD 1 | LOD 2 |
|---|---:|---:|---:|
| Minimum projected height | 200 px | 60 px | 0 px |
| Triangle ceiling | 6,000 | 1,200 | 400 |
| Post-split vertex ceiling | 12,000 | 2,400 | 800 |
| Logical mesh / material / draw per visible part | 1 / 1 / 1 | 1 / 1 / 1 | 1 / 1 / 1 |
| Base-color map | 512² | 256² | 64² |
| Normal map | 512² | 256² | None |
| Metallic/roughness map | 512² | 256² | Factors only |

Use at least two clear cap/chamfer transitions at every LOD. LOD 1 keeps the
functional mounting silhouette. LOD 2 retains a small peg silhouette and the
coral bow shape, without detailed wells or tiny fasteners. Prefer restrained
wear variation over aliased black scratches. Main exposed surfaces should
receive roughly 96 texels/m at LOD 0; important mounting faces may receive 128.
Do not stretch a tiny atlas patch across the whole side just to meet file size.

Those maps total **3.765625 MiB of base-level RGBA8** across the three LODs.
Current `MeshPath` builds full mip chains, so requested GPU texture storage is
approximately 5.02 MiB before driver overhead. VMESH embeds decoded pixels;
three small PNG files are not the runtime memory figure. At the vertex/index
ceilings, the three LODs add approximately 1.14 MiB of geometry. Target under
8 MiB combined requested resident geometry/textures for the complete pontoon
LOD set, with repeated part instances sharing the same uploaded asset.

This assumes the one-material-per-LOD layout. Current material uploads can
duplicate a referenced image for each material or separately uploaded mesh;
report the actual allocation layout and do not assume cross-LOD/material
deduplication already exists. Record source/decoded/upload/mip/staging counts
separately. These per-asset limits do not prove the later 8,192-part world
budget, streaming, or GPU retirement behavior.

Use embedded PNG textures, UV0 only, repeat addressing, linear/trilinear
sampling and core opaque metallic-roughness shading. No separate AO, alpha
blending/masking, texture transforms, extra UVs, vertex colors, skins,
animations, negative scales, transmission or unimplemented material extension.
The renderer confirmed this design fits the proposed `salvage-rigid-v1`
profile. Its final CLI is:

```text
gltf_vmesh_tool --profile salvage-rigid-v1 input.glb output.vmesh
```

That agreement is a schema review, not an ASSET-03 passing result. Use the
completed ASSET-03 evidence and rebuilt actual tool before cooking this asset.
Never fall back silently to the legacy import path to make an export pass.

## 6. Source, provenance and reproducible outputs

Suggested implementation boundary: a new `tools/salvage_assets/author_pontoon.py`
plus a checked parameter specification; keep `authoring_probe.py` and its
historical fixture unchanged. The script must validate finite bounded sizes,
tick-exact physical parameters, socket fit, positive masses, output paths and
array/geometry budgets before doing expensive work. Emit geometry, proxies,
metadata and preview framing from the same parameter source.

Use a fresh output directory and preserve earlier published bundles. Start
Blender with the existing `--background --factory-startup --python-exit-code 1`
workflow. Recheck the actual Blender version; ASSET-01 used 5.2.1 LTS. Use a
fixed seed and a bounded CPU render or an explicitly coordinated GPU slot.
Interactive/MCP edits must be captured back into parameters/scripts or retained
as an explicit source dependency. A saved `.blend` alone is not a regeneration
recipe. Do not require unavailable Blender MCP to perform this work.

Proposed asset directory: `data/salvage/pontoon/v2/`, published only when its
candidate is accepted. Keep the following deliverables together:

| Deliverable | Required content |
|---|---|
| `source/pontoon.blend` | Editable named shell/mount/material/preview collections; packed maps; separate non-export gameplay/axis helpers |
| `source/parameters.json` | Definition/version, dimensions, station/socket/proxy IDs, mass inputs, palette, seed and LOD limits |
| `source/pontoon-lod-{0,1,2}.glb` | Self-contained strict-profile exports; only selected render nodes; no helpers/cameras/lights |
| `source/maps/` | Original base-color, normal and MR PNGs for each applicable LOD, with channel/color-space declarations |
| `source/pontoon.gameplay.json` | Schema-1 sidecar with exact source GLB hashes/byte lengths, per-LOD basis, physical data and tool anchors |
| `cooked/` | Atomic cooker output: normalized `gameplay.json`, `cook-manifest.json`, `lod-<durable-id>.vmesh` files |
| `preview/thumbnail.png` | 512² isolated three-quarter view, full object visible with safe margins |
| `preview/turntable.webm` | Declared full 360° turn, approximately six seconds, no depth-of-field hiding fit defects |
| `preview/contact-sheet.png` | Front/rear/left/right/top/bottom plus assembled socket close-up |
| `provenance.json` | Source/control/tool hashes, actual versions, export/render settings, map/channel origins, licensing and review state |

Use stable LOD IDs `1`, `2`, `3`; keep them independent of glTF array order.
Allocate distinct visual content keys in the project asset namespace/registry
and record them explicitly, without deriving identity from mesh indices or
filenames. The part definition key above is already reserved; visual counters
must be reserved without colliding with other authored assets.

Store original project-authored geometry/map provenance. If generated raster
art or external fonts/textures become useful, retain the exact prompt/input,
actual provider/model when exposed, source/license and the transformed map
outputs. Do not attribute a selected model version the tool did not disclose.
The concept is reference-only and need not become a texture dependency.

Keep the cooker contract from [ASSET-02](../../../../tools/salvage_assets/GAMEPLAY_SIDECAR.md):
exact source digests, self-contained GLB, normalized metadata preserved, real
strict converter and shared C++ validator, exact tool/output digests, atomic
publication into a new directory. A renamed source with stale metadata fails.
Do not invent shell commands for the new authoring script before its CLI
exists; record its exact reproducible invocation with the final evidence.

Regenerate twice into fresh directories. Compare GLB, normalized sidecar,
VMESH and decoded preview pixels. Record PNG metadata or `.blend` binary
differences honestly; ASSET-01 already demonstrated why file-byte equality of
those formats is a different claim. Preserve the tested parameter/script/tool
hashes and successful commands, including any profile compatibility failures.

## 7. Engine fixture and acceptance sequence

ASSET-04 requires real native and browser rendering of the same asset/metadata,
not only a successful offline cook. The existing renderer uploads geometry but
does not provide a general canonical prefab adapter merely because VMESH stores
nodes. Follow the [identified runtime bridge work](../LOOK-01/investigation.md).
Coordinate shared loader/build/application changes with root; this document
does not authorize a second conflicting implementation.

Keep the first fixture dry and static so scale, socket geometry and material
errors are visible without depending on future compound physics. Add these
checks in dependency order:

1. **Cook and registry.** Validate v2 with the actual C++ catalog. Verify source,
   normalized sidecar, exact VMESH and selected converter/profile identities at
   runtime bundle admission. Unknown versions, missing LODs and tampered output
   reject with an asset/property error. Keep the previous valid fixture intact
   on failed admission. Both v1 and v2 exact lookups remain testable.
2. **Rigid prefab bridge.** Retain and evaluate glTF node hierarchy, including
   multiple nodes sharing a mesh. Use full composed matrices and correct normal
   transforms; a translation-only fixture cannot prove this. Apply the recorded
   LOD basis once. Validate node indices, traversal and instance/draw limits
   before publication; reset/leave/reload cannot leave stale mappings.
3. **Axis and winding stand.** Draw the asymmetric axis fixture and v2 beside
   metre/plate rulers. Orbit all sides with single-sided back-face rejection
   active (the current renderer uses material-dependent shader discard). Check the
   forward mark, canonical right, top peg height, .96 m body height, 4 m length
   and actual hierarchy-derived render bound, including the .18 m pegs.
4. **Exact socket fixture.** Place v2 pontoons at `{−75,0,0}` and `{75,0,0}`
   ticks. Place existing v1 beams crosswise at `{0,48,−50}` and `{0,48,50}`
   ticks, all rotation 0. Beam bottom sockets at X ±75 ticks mate to pontoon
   forward/aft top sockets. Add a second v2 at `{0,48,0}` over a v2 at origin
   to check central stacking. Validate through `BuildModel`; render socket
   axes/clearances and actual peg/well engagement with overlays toggled off too.
5. **Awkward rotations.** Apply all 24 proper rotations to a complete valid
   two-part fixture, including sideways/upside-down and a negative translation.
   Exact socket origins/normal opposition/+X keys and occupancy still agree.
   A one-tick displacement and a wrong key rotation reject. Test both sides of
   a sector boundary so camera rebasing cannot separate mesh and metadata.
6. **Material diagnostics.** Actually draw a four-quadrant labeled UV chart and
   a directional tangent-space normal chart, then the pontoon material. Check
   top/bottom and left/right in native and browser, plus moving directional
   light on a fixed view. Detect flipped UVs, wrong normal handedness, wrong
   sRGB/data treatment and double tint. Merely uploading a textured asset does
   not prove it was sampled in the capture.
7. **LOD and resource checks.** Exercise fixed LODs and thresholds with a slow
   camera move. Preserve bounds/socket cues, avoid obvious silhouette pops,
   and report actual counts/allocations and missing staging/driver categories.
   Selection uses projected size from the declared view, not separate native
   and browser hardcoded distances. Show repeated parts share resident assets.
8. **Package and replay.** Register source/cooked/map dependencies in both build
   systems and web packaging; do not rely on a loose source-tree file. Capture
   fresh native and WASM packages at the same measured physical viewport,
   camera, FOV, exposure, lighting and LOD. Record logical size/DPR separately;
   this host's scaling can make equal window arguments produce unequal pixels.

The existing beam prototype is allowed in the socket fixture and must be
labeled. Its box visual does not have a real bottom well, so it proves metadata
fit but cannot demonstrate final mesh nesting there. The v2-on-v2 close-up
must demonstrate actual visible peg/well nesting. The full authored beam/plate
and first boat layouts follow in ASSET-05; do not claim that kit from this
single-part test.

Mesh helpers must not be submitted twice by the legacy primitive path. Keep
static test ownership separate from a live simulated `GameSession`; no fake
physics body or inventory grant is needed to display the technical stand.
Ensure the asset path does not require launching the legacy motorcycle mode.

Record a neutral daylight three-quarter shot, all-side contact sheet, close-up
engagement, material diagnostic images and a short moving-camera capture in
both native and WASM. Use a declared matching physical viewport, initially
1920×1080 where available. Record exact package/source hashes and captures;
offline preview tonemapping is a separate reference from engine exposure.

A wet side view may expose the already identified water/opaque ordering gap;
report it honestly. It cannot prove correct submersion/refraction or waterline
physics. The dry technical fixture may pass ASSET-04 once every listed asset
check passes, while LOOK-01 stays pending until its visible water/composition
defects are resolved. Full dynamic composition remains REND-01–04.

## 8. Completion and handoff

An independent reviewer must inspect the actual generated source/cook records,
physical metadata calculations, native/browser captures and rendered fixture.
They must identify any approximation and distinguish measured results from
design intent. Required evidence includes the final source/tool identities,
reproduction commands, actual texture/mesh counts, socket/volume bounds,
unsupported-feature failures, and any nondeterministic output differences.

ASSET-04 is complete only after the plan's full pipeline acceptance passes.
This specification, a generated file set, a strict-profile parser pass, or a
pretty thumbnail is insufficient. Root owns the task checkbox and gate commit.
The next outputs are the kit in ASSET-05, material standards in ASSET-06 and
the real walkable cove in LOOK-01. Final production art, damage variants and
owner approval remain VIS-03/G08.
