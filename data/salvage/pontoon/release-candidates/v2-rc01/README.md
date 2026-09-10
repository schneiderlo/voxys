# Pontoon v2 release candidate

This is a staged asset package, awaiting independent pipeline review. It is not
published, is not accepted cove art, and does not change installed game content.

The editable source, three GLBs, seven original maps, parameter specification,
sidecar, cooked VMESHs, 512-pixel thumbnail, contact sheet and six-second
turntable are included. The original 640-pixel thumbnail is retained under a
separate name. Preview images use Cycles/AgX; engine lighting is a separate
reference. No texture contains baked directional lighting or external imagery.

The part key is namespace 766f7879732d73616c766167652d7631, counter 3, version 2.
The original prototype version 1 remains distinct. Width/body height/length are
1/.96/4 m; engaged stacking advances .96 m; pegs extend .18 m. Placement uses
.02 m ticks, +Y up, -Z forward. The exporter-to-canonical proper rotation is 12,
applied once. Source LOD files 0/1/2 map to durable LOD IDs 1/2/3. The sidecar
contains authoritative socket, collision, buoyancy, mass and LOD metadata.

## Regeneration

Run from the repository using Blender 5.2.1 LTS build 9e2066aef7ef and its Python
modules. Use fresh output directories. Substitute this package's absolute path
for PACKAGE; these commands never overwrite this package.

```sh
blender --background --factory-startup --python-exit-code 1 --python PACKAGE/recipes/author_pontoon.py -- --spec PACKAGE/source/parameters.json --output-dir /tmp/new-pontoon --preview full
python3 PACKAGE/recipes/cook_gameplay_asset.py --sidecar /tmp/new-pontoon/source/pontoon.gameplay.json --sources /tmp/new-pontoon/source --output /tmp/new-pontoon/cooked --converter build-salvage-native/bin/gltf_vmesh_tool --validator build-salvage-native/bin/gameplay_sidecar_tool
blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python PACKAGE/recipes/render_pontoon_review.py -- --candidate /tmp/new-pontoon --output /tmp/new-pontoon/review
python3 PACKAGE/recipes/pontoon_contact_sheet.py --candidate /tmp/new-pontoon --output /tmp/new-pontoon/review/contact-sheet.html
ffmpeg -framerate 12 -i /tmp/new-pontoon/preview/turntable-frames/%03d.png -c:v libvpx-vp9 -crf 26 -b:v 0 /tmp/new-pontoon/preview/turntable.webm
```

Build the strict converter/sidecar validator with the repository Nix toolchain.
The cook manifest records exact executable hashes; an identical payload cooked
with a different executable has a different provenance manifest. Clean r05/r06
runs reproduced GLB, maps, sidecar, VMESH and decoded static preview pixels.
Blender/PNG container bytes can differ. Repeated turntable pixel equality was
not established. The contact-sheet PNG is a capture of the included HTML; it
is not an independently rendered view.

The full implementation plan and runtime review are in
GAME_IMPLEMENTATION_TODO.md and docs/validation/salvage/ASSET-04/ in the repository.
They include real native/browser material, socket, hierarchy, rotation, LOD,
sector and ownership evidence. History files preserve the original candidate's
records; their paths and output lists refer to that original candidate, whose
72 raw turntable frames are intentionally not duplicated here. This package's
own provenance.json inventories all files actually included here.

Before publication, independently review the complete source/runtime evidence,
confirm native/browser loading of the final manifest, and record acceptance.
Then publish one immutable v2 bundle and update explicit registry selections;
do not silently replace prototype v1, old saves or another accepted v2 bundle.
