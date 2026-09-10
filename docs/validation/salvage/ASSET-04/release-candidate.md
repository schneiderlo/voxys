# Pontoon release candidate preparation

A complete package is staged at
`data/salvage/pontoon/release-candidates/v2-rc01/`. The subsequent
[exact-manifest runtime check](release-runtime.md) now passes in native and
browser inspection. Independent review and immutable publication remain open.
The original v1 definition and r04 files remain intact; previous registry
selections are archived with that runtime check.

`tools/salvage_assets/prepare_pontoon_release.py` verifies the original r05
provenance and supplementary artifact hashes, checks current authoring recipes
match the recorded generator/specification, then assembles the package into a
fresh directory. It cooks again using the current real converter and sidecar
validator. A changed normalized sidecar or VMESH compared with the reviewed
candidate fails preparation. No interactive Blender scene is touched.

The package includes 49 inventoried payload/reference files plus its own
`provenance.json`: editable Blender source, parameters, three GLBs, seven maps,
sidecar, four runtime payloads and cook manifest, 512-pixel thumbnail, original
640-pixel thumbnail, all-side/contact views, contact-sheet HTML and PNG, a
six-second turntable, frozen authoring/review recipes, previous records, current
cook log and self-contained regeneration instructions in `README.md`.

Every included file's bytes and SHA-256 were verified. Normalized metadata and
all three VMESH files are byte-identical to the installed r04 payloads used for
the actual native/browser inspections. The fresh cook manifest differs because
it truthfully records the current executable hashes:

| Identity | SHA-256 |
| --- | --- |
| New cook manifest | `5cb23035a14272257af22ae4af47e7fcd863b8b310186346acbf7915fba8818e` |
| Converter executable | `9de1ede050e226e444ab3c2dbe44e358c6f0ca04d299de9582b9d6ea1e1a93fa` |
| Sidecar validator executable | `837b01c0ddcfdec1bbc48442aff6f1fb9a7d330203958c1591d9b955c7f68858` |

Preparation alone did not prove runtime admission. The linked subsequent check
records actual admission of this exact manifest. Before immutable publication,
obtain independent review of the full pipeline using the
[review handoff](REVIEW_HANDOFF.md). Do not mark ASSET-04 complete from payload equality alone.
Do not overwrite any accepted bundle under the same content key.

Reproduction from the repository root, using a fresh output whose parent exists:

```sh
nix-shell --run 'python3 tools/salvage_assets/prepare_pontoon_release.py --candidate data/salvage/pontoon/candidates/v2-r05 --output /tmp/new-pontoon-release-candidate --converter build-salvage-native/bin/gltf_vmesh_tool --validator build-salvage-native/bin/gameplay_sidecar_tool'
```

The package README contains clean Blender generation, cooking, thumbnail,
contact-sheet and turntable commands. Original authoring used Blender 5.2.1 LTS
build `9e2066aef7ef`, seed 1979, original procedural maps and no external imagery.
The r05/r06 checks reproduced GLB/maps/sidecar/VMESH and decoded static preview
pixels. Blend/PNG containers differ; repeated turntable pixel equality was not
established. The 72 original raw turntable frames remain in r05 and are not
copied into this compact package. Historical records explicitly refer to their
original candidate; the new manifest inventories what is actually included.

Independent review should cover `design.md`, `runtime-design.md`, `materials.md`,
`assembly.md`, `native-motion.md`, `hierarchy.md`, `rotations.md`, `sectors.md`,
`hierarchy-bounds.md`, this package and the linked raw results. Distinguish root's
technical checks from independent review, GPU reservation from total memory,
static inspection from boat physics, and the dry asset fixture from LOOK-01's
unfinished cove art and water composition. No G02 or visual acceptance is claimed.
