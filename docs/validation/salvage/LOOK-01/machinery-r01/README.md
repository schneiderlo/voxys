# Molded Cove machinery

Prepared 2026-09-12. Final installed presentations are **helm/winch r02, engine
r03, and corrected propeller r04**. The first r03 propeller is superseded and
never ships. `data/salvage/cove-workshop-r04.json` selects seven replacements:
the three earlier structural parts plus these four machines. The three paid
individual-brick catalog entries remain unchanged.

Focused CPU/GPU checks, both builds and the actual native/browser paint/save
journeys pass. Final publication checks remain pending here. The
preceding published checkpoint is `40e84269`; this is candidate checkpoint
evidence, not a completed LOOK-01 gate or owner visual approval. No rendered
images, screenshots or video were produced.

## Appearance, identity and ownership

The original procedural models use cream molded bodies, teal panels/guards,
orange accents, dark relief and steel working surfaces. Editable `.blend`, three
GLBs, source metadata, cooked payloads, manifests and provenance accompany every
part. Blender 5.2.1 LTS authored these assets; no external models, generated
textures, images, logos or Blender MCP were used.

Materials use solid PBR factors with no textures, UV charts or tangents. Palette:
cream `F0DDB2`, teal `197D86`, orange `ED7942`, slate `253D53`, steel `B5C3BE`,
rubber `25363A`; values are converted from sRGB to linear. Plastic roughness is
0.32, rubber 0.72; only steel has metallic factor 0.8. Geometry uses manufactured
surface normals. This does not satisfy future texture-density/material gates.

Canonical keys, mass/inertia, collision, occupancy, buoyancy, sockets, module
behavior, strengths, costs and salvage yields match the installed original
non-LOD metadata exactly. Originals are `material-calibration/r04/kit` for
helm/winch and `functional-kit/r09` for engine/propeller. LOD IDs 1/2/3, thresholds
200/60/0 and render-to-canonical rotation 12 remain unchanged. New visual IDs use
namespace `voxys-toy-art-v1`, version 1:

| Part | Canonical counter/version | Visual counters | Near / middle / far vertices | Draws per LOD | All-LOD requested GPU bytes |
|---|---|---|---|---|---:|
| Helm r02 | 6 / 2 | 401–403 | 2728 / 1104 / 718 | 6 | 423,168 |
| Winch r02 | 7 / 2 | 501–503 | 4684 / 1900 / 692 | 5 | 686,448 |
| Engine r03 | 4 / 2 | 601–603 | 2332 / 1034 / 644 | 6 | 371,592 |
| Propeller r04 | 5 / 2 | 711–713 | 1512 / 680 / 336 | 4 | 238,752 |

The catalog pins both the canonical source manifest and replacement manifest.
Final propeller manifest SHA-256 is
`390120c23e8e062886c92ff358d5e50dc9ba63df7b73ca114f86a8388da40812`.
Full source/payload hashes and metrics are in `helm-winch-report.json`,
`power-final-report.json`, the installed provenance and cook manifests. The
power report also inventories the explicitly superseded scratch propeller;
that inventory is not the shipped catalog.

## Review findings and correction

Independent read-only review checked canonical metadata equality, all source and
cooked links, finite/unit normals, outer bounds, new visual-ID uniqueness,
unchanged catalog entries and exact selectors. The first two author recipes
recorded their scratch-only self path in provenance. Their self keys and
installed reproduction commands were corrected; provenance preserves the
original authoring hash. This metadata-only fix changed no asset payload.

Outer-bound and metadata checks did **not** prove internal mechanical clearance.
A later targeted source review found the inherited propeller blade intersected
its guard, including point `(0,0.400,0.070)` metres. The initial r03 candidate
passed strict cooking and budget checks but is rejected for this real geometry
defect. Its counters 701–703 and bytes remain immutable in scratch; original
report, provenance, source metadata and cook/author logs are retained under
`superseded-propeller-r03/`. The installed original power recipe can reproduce
that historical pair; it must not supply the final propeller.

The separate `author_cove_propeller_art.py` recipe creates r04 with new counters
711–713. Blade center is `(0,0.205,0.070)` and half extents
`(0.075,0.140,0.040)`. All un-beveled blade corners fit within radius 0.353058 m;
the coarsest guard's inner apothem is `0.380*cos(pi/12) = 0.367052 m`. This gives
**13.994 mm conservative radial clearance** for all three blades and LODs.
Bevel only increases that clearance. It proves blade/ring fit, not animation or
whole-assembly motion. Guard, hub, shaft endpoints and gameplay stay unchanged.

Helm/winch remain inside the old outer envelope. Engine decorations expand it
by at most 17 mm; propeller cap by 10 mm, inside the 20 mm bound and away from
the mating endpoint. Structural wells retain the existing cross-LOD keyed
profile; added foot studs/fasteners remain outside it. Recorded winch
flange-to-cheek clearance is 10 mm. An outer AABB is not a surface-to-collision
distance proof. Wheel, drum, engine and propeller are still static meshes.

## Executed evidence

- All four final strict `salvage-rigid-v1` cooks and sidecar validation pass.
  Author/cook logs and reports are retained in `authoring/` and `checks/`.
- The first combined fixture/gameplay/accounting run passed eight cases; the
  separate material target passed four, all zero skips. These original results
  are retained in `../../UX-01/brick-paint-r01/checks/` and used the first
  propeller candidate. They do not establish its mechanical clearance.
- After correction, **only the two affected** registry/admission cases reran:
  `checks/final-machinery-r01-test.xml`, two passes, zero skips. The registry
  checks canonical parts/transforms; the real Vulkan GPU admits the final
  catalog at **10,740,488 owner bytes and 78 color draws**, versus 14,522,896
  previously observed bytes. The unchanged owner/resident limits are 16/48 MiB.
- Final native/WASM builds pass. Exact package hashes and the actual ten-stage
  native and seven-stage browser journeys with seven presentations are retained in
  `../../UX-01/brick-paint-r01/`. Native places/repaints/saves/restores one brick
  while the final machines are loaded. Browser reload also preserves the exact
  painted design/ownership; its completed outer report has no browser errors.

Blender completed each export, `.blend` save and authoring check, then hung in
the sandbox's PulseAudio shutdown. Completed processes were interrupted (power
exports record exit 130). Logs retain completion markers and shutdown warnings;
these are not claimed successful Blender process exits. Final recipes disable
file thumbnails. An earlier helm cook attempt was blocked before cooking by
the Nix daemon sandbox; its failure and successful permitted retry are retained.
No repeated rendering or recooking was used for this documentation.

## Reproduce from installed repository paths

Run from repository root with Blender 5.2.1 LTS and fresh output directories.
Do not overwrite a published visual ID with changed bytes.

```sh
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_machinery_art.py -- --output-dir <new-helm-winch-directory>
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_power_art.py -- --output-dir <new-engine-directory>
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_propeller_art.py -- --output-dir <new-propeller-directory>
```

Use only the engine from the second command; its historical propeller is
superseded. For each final chosen part, inside the repository `nix-shell`:

```sh
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar <new-directory>/<part>/source/<part>.gameplay.json \
  --sources <new-directory>/<part>/source \
  --output <new-directory>/<part>/cooked \
  --converter build-native-save-host/bin/gltf_vmesh_tool \
  --validator build-native-save-host/bin/gameplay_sidecar_tool
```

Recipes import only installed repository helpers; their hashes are recorded in
each provenance file. Blender/exporter versions can change serialization; no
cross-version bit-identical export is claimed. Strict manifests identify the
actual committed bytes. To repeat final admission, run `//tests:voxy_tests` with
the two exact case names/filter retained in `checks/final-machinery-r01-test.log`.
Do not replace gameplay bundles or add duplicate presentation selectors.
