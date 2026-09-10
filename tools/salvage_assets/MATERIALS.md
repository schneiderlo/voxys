# Salvage material and texture contract

Status: ASSET-06 preparation, 2026-09-08. No material or visual gate has passed.
The game and acceptance requirements remain in GAME_IMPLEMENTATION_TODO.md.
The installed pontoon v2-rc01 and kit r09 remain historical inspection inputs.

## Appearance and material families

Use broad warm craft colors, restrained coral safety accents, cool dark grip
surfaces and readable exposed metal. Molded edges, seams and socket geometry
carry the form. Fine scuffs should become apparent near a surface, rather than
breaking the silhouette or turning the whole boat into noisy stone.

Values below are calibration targets, not measurements of a real material.
Base color is stored as sRGB RGB; roughness, metallic and tangent normals are
linear data. Roughness here is perceptual roughness, squared once by GGX.

| Family | Base palette / use | Dry roughness | Metallic | Wet target |
|---|---|---:|---:|---|
| Cream plastic | D9C9A2, hull/deck/body | .34 | 0 | Thin dielectric coating; .16 outer roughness |
| Teal plastic | 2A6767, rails and molded trim | .40 | 0 | Same coating; preserve underlying teal |
| Coral plastic | CF6548, warnings and tool guards | .36 | 0 | Same coating; preserve safety readability |
| Dark polymer | 354852, dark panels | .73 | 0 | .30 outer roughness |
| Rubber | 232D30, dedicated grips/hoses later | .78 | 0 | .36 outer roughness, no metal tint |
| Bare metal | 869498, shafts/latches/flanges | .30 | 1 | Dielectric film over conductor; do not change substrate metallic |
| Clear glass | Near-neutral transmission, framed instrument covers later | .06 | 0 | Water film and droplets only when supported |

The r01 opaque calibration implements the first five atlas regions only.
The separate metric candidate gives the helm wheel a dedicated rubber material.
Instrument panels do not
count as implemented glass. Painted metal uses dielectric paint until exposed
substrate is authored. No blanket gray metallic material on painted parts.

Keep conductor/dielectric identity and average roughness consistent across all
geometric LODs. Far can remove small bevels or omit a normal texture after its
roughness accounts for lost detail; it cannot turn the metal into plastic.
Keep palette region assignment tied to the actual authored surface/domain.
A fixed-radius face-center guess is insufficient for changing socket profiles.

## Current calibration implementation

Both Blender recipes accept `--material-profile opaque-calibration-v1`.
`material_standards.py` owns the shared dry response. This candidate emits a
base-color atlas and a metallic/roughness atlas at every LOD, with neutral
material multipliers. No base-color lighting, random color grain or unscaled
normal noise is emitted. This gives a clean reference for evaluating geometry,
light response and later physically scaled surface detail. It is explicitly
unfinished surface art; do not advertise flat calibration as tactile finish.

The `legacy` default preserves old GLB generation behavior for original
recipes. Reproduce immutable old candidates with their frozen recipe files;
new provenance also includes the shared material module's digest. Create a new
output directory for each candidate. Do not overwrite installed bundles or
reuse a published content identity with different bytes. Candidate revisions
are not save migrations or final publication.

## Detail, decals and texture budgets

The opt-in `tactile-metric-v1` profile is implemented in `metric_materials.py`
and both Blender recipes. It uses linear palette factors and repeating normal/
MR tiles with a .5 m period: Near 128² gives 256 texels/m, Middle 64² gives 128,
and Far retains palette/roughness/metallic factors without detail images.
This first candidate uses the structural density on the helm grip too; the
512 texels/m hand-contact target and dedicated rubber microstructure remain
future refinements. Do not call this finished tactile art.

Orthonormal face charts remove atlas stretch and palette bleed. The pontoon
classifies original shell faces, then triangulates before charting because
some Boolean n-gons are nonplanar. Hard-edge charts can change texture phase;
the candidate does not claim seamless detail across every bevel. The height
field is periodic, with eight bounded Fourier modes at 2–9 micrometre
amplitudes. Analytic physical slopes supply tangent normals. PNG rows run down,
authored V runs up; the generator evaluates `v = 1 - (row + .5) / size` and
uses the authored tangent frame retained by Blender's glTF export.

The roughness tile has mean .97 with ±.025 bounded modulation. Material factors
compensate for its mean; R/B/A stay neutral and the conductor identity comes
from metallicFactor. The exporter is checked for retained maps/factors/repeat
sampling. The source tile can be reused, but current runtime textures are
allocated per material. Six families add material draws; report that cost.
The first three-part fixture requests 5,564,672 GPU bytes, including residency
for all three detail levels, under the unchanged 16 MiB owner limit.

See `docs/validation/salvage/ASSET-06/metric-r01/` for actual GLB measurements,
normal-orientation checks, retained failed chart attempt and engine captures.
Current Far roughness is the declared dry target; normal-variance filtering,
bright/dark exposure calibration, continuous-motion inspection and wet/glass
work still prevent parent acceptance.

The kit's opt-in `--surface-shading manufactured-v2` adds analytic ring,
rounded-box and cylinder normals. Broad panels/caps retain their flat normals;
curved barrels and bevels interpolate smoothly. Socket-cut components use a
weighted fallback with hard-edge boundaries. Six winch/helm GLBs and matched
native/browser views are checked in `docs/validation/salvage/ASSET-06/shading-r02/`.
Their geometry, winding, UVs and dry materials stay identical to the flat r02
baseline; the current inspection requests 5,485,472 GPU bytes. Other parts,
socket-cut shading, chart seams and moving-camera behavior still need work.
Use each archived candidate's complete recipe closure for replay; the latest
record also documents the missing `authoring_probe.py` helper in the old r02
archives without rewriting those historical inputs.

- Author surface detail in metres. Initial slice target: 256 texels/metre for
  close structural surfaces, 512 for hand/tool contact and readable labels,
  128 for secondary distant scenery. Treat these as per-surface targets;
  record world-area/UV-area measurements and reject unintended anisotropic
  stretch above 2:1. Tiny bevel charts may be exempt when documented.
- Existing palette-atlas Smart Project UVs do **not** meet that contract.
  Use metric UV charts or a separate metric detail layer before adding grain.
  Preserve canonical placement and gameplay geometry while revising UVs.
- Use reusable 512² material tiles/trim sheets first. Keep unique maps at ≤1024²
  in the slice unless the budget report justifies a specific exception. Pack
  roughness in G and metallic in B. R is 1 when no separate AO is supplied;
  no baked shadow in base RGB. Tangent normals use +Y green, neutral (128,128,255).
- Keep small labels in a shared atlas; avoid a material/draw for every sticker.
  A label needs adequate pixel height or an icon fallback. No unreadable fake
  text. Baked marks must remain surface-aligned through UV and LOD changes.
- Extrude atlas borders and verify the last sampled mip before neighboring
  regions mix. Full mip chains exist in MeshPath: RGB filtering occurs in
  linear space and normals are renormalized. This does not by itself prevent
  cross-region bleed or filter normal variance into roughness.
- Current inspection ownership stays **16 MiB per fixture / 48 MiB for three
  owners including retirement**, with the existing fixed reservations. Count
  actual requested image bytes including all mips, vertices, indices, material
  blocks, guides, duplicates and pending retirement. Do not multiply texture
  size until memory passes. Sharing a PNG in authoring does not prove the
  runtime deduplicates its GPU allocation.
- Current kit maps are 128/64/64 and pontoon maps 512/256/64. Their resident
  image sizes are inspection baselines, not proof of production texel density.
  Production larger tiles require measured sharing and bounded admission.

## Baking and optional generated imagery

Keep the original source model, UV layout, high/low geometry, cage, Blender
version, export settings and input hashes. Apply transforms and validate the
canonical basis before baking. Bake tangent normals from actual geometry or a
metric height field. Generate roughness from an explicit material recipe or
author-painted masks; a grayscale photo is not automatically roughness.
Inspect normal/roughness under at least two opposing lights and in motion.

Optional ImageGen color prompt template: "Seamless, orthographic unlit material
color only; original [surface] in [palette], subtle [wear scale in metres]; no
cast shadow, highlight, ambient occlusion, bevel shading, perspective, text or
logo." Record the actual prompt, tool/model identity when returned, source
references and original output. Inspect all tile edges and remove fixed
lighting before use. Do not claim generated RGB supplies calibrated normals,
roughness, collision or a finished PBR asset. No generated imagery is used in
the present calibration candidate.

## Wetness, glass and renderer prerequisites

The strict `salvage-rigid-v1` importer and runtime prefab validator currently
require opaque materials. Generic MeshPath blend support is not a validated
salvage glass path. Glass needs explicit profile/metadata admission, sorted or
order-independent transparency, appropriate depth ownership, correct normal
and backface treatment, and bounded scene-color transmission inputs. Start
with a thin clear pane; define IOR 1.5 and geometric thickness explicitly.

Wetness needs a coherent water-film lobe (IOR 1.333), substrate attenuation,
roughness coverage and bounded per-instance wet state. A static low-roughness
swatch is a calibration target, not simulated wetness. Do not bake specular
highlights or darken metallic base color arbitrarily to imitate water.
Complete linear HDR composition and one exposure/output conversion under
REND-01–04; no sampled attachment feedback. LOOK-01 can use an opaque stationary
craft but cannot pass with broken water/opaque ordering.

## Required acceptance before ASSET-06 can close

1. Corrected opaque candidate cooks and renders in both applications. Metal,
   palette regions and roughness remain coherent through all LODs.
2. Add metric surface detail and dedicated rubber. Inspect actual native and
   browser views at matching resolution under neutral, bright, dark and
   opposing directional light; include a short moving-camera capture.
3. Implement and inspect wet response and the explicit glass path above.
4. Record texel density, atlas bleed, requested/resident/transient memory,
   texture ownership, missing-map rejection and reproducible provenance.
5. Resolve independent review after ASSET-04 prerequisite acceptance. Preserve
   owner feedback and keep LOOK-01/final art gates separate.
