# Woodland trail raider

Original brick minifigure enemy appearance for the first trail encounter. A brown molded hood, determined yellow toy face, moss sleeves, earthy jerkin, rust strap and blunt wooden staff distinguish the raider from the friendly village residents. No external character art, textures or logos are used.

## Runtime contract

- Identity: `voxys-adventure-raider-r01`.
- Loader: `loadRaiderAsset(provider, error)` or `loadRaiderAsset(directory, error)`, returning `shared_ptr<const RigidAnimationAsset>`.
- Profile: `salvage-animated-rigid-v1`, schema 1, render-to-canonical basis **12**. Apply that basis exactly once through the existing rigid animation path.
- Canonical coordinates: metres, +Y up, −Z forward, origin at the standing feet. Keep instance scale 1 and tint white; the authored yellow skin and clothing colors are already baked into materials.
- Near / mid / far order: `character.vmesh`, `lod1.vmesh`, `lod2.vmesh`. The current strict loader admits the near file named in the manifest; additional LOD runtime selection is separate integration work.
- Each level has 22 nodes, 15 meshes, 31 expanded draws, 8 clips and 37 animation channels. No skins, morph targets, textures or root motion. Historical `robot_*` node names are the shared technical rig ABI, not the visible character design.
- The manifest retains the existing caps: 32 nodes, 24 meshes, 48 draws, 2 MiB model file and 1 MiB GPU geometry. It does not relax player, resident or legacy robot admission.

| Level | File bytes | GPU geometry bytes | Vertices | Triangles |
| --- | ---: | ---: | ---: | ---: |
| Near | 744,851 | 750,920 | 9,486 | 5,618 |
| Mid | 396,587 | 387,536 | 4,859 | 3,098 |
| Far | 216,011 | 198,320 | 2,471 | 1,658 |

Manifest SHA-256: `5e1306b0e6a4465d9c9b7d6db940da95b4e6c3dcfa40162d6e8eeb0de92a27fd`.

Every cooked/source hash is recorded in `manifest.json` and `provenance.json`. The manifest binds the near bytes and records all three strict cooker results. `proof.json` binds the actual proof to its editable source and rendering recipe.

## Rig, staff and physical limits

The inherited clips are `idle`, `walk`, `jump`, `fall`, `swim`, `helm`, `tool` and `land`. These are rigid toy-limb animations, not skinning, cloth or a finished combat animation set. `tool` supplies a preliminary lifted-staff gesture. A readable combat wind-up, impact and recovery pose sequence still needs authoring and gameplay timing.

The solid 0.806 m staff is joined to `robot_hand_r`, through the existing C-grip. It follows that node without adding a new mesh, node or anchor. It is cosmetic: it does not create a weapon collider or authorize damage. Melee hit timing, range and obstruction checks belong to the gameplay authority.

All three actual exported models stand 1.70000005 m high and 0.584 m wide. The head, torso and pelvis satisfy the existing 0.3 m capsule cap check. During walking, width is 0.584804 m and the crown reaches 1.726753 m because the shared rigid hip gait lifts the feet off the floor. Feet remain above the floor in sampled walk, land and fall clips. The 1.36 × 2.24 m starter doorway leaves 0.775196 m total width clearance and 0.513247 m overhead clearance for this walking envelope.

The staff extends beyond the body capsule: bind radius is about 0.809 m; the near Tool sample reaches −0.9673 m along the forward axis. Existing jump/swim clips have wider visual envelopes too. These sampled visual bounds are not continuous weapon sweeps or guarantees against cosmetic wall intersections. Do not enlarge the actor's movement capsule to the full staff envelope.

## Editable source and checks

`source/character-lod-{0,1,2}.blend` are editable Blender authoring files. Their matching GLBs are the exact inputs consumed by the strict cooker. `tools/adventure_assets/generate_raider.py` reuses the existing player proportions, hierarchy, clip generator, resident geometry helpers and unchanged GLB pruning/cooking conventions. It writes only new output directories. Existing player and resident packages are hashed before and after authoring and checked again when cooking/validating.

The source checker samples actual exported vertices, validates unit normals, checks the body capsule and walking envelope, and verifies both yellow C-hands retain their inner opening and bottom gap. It distinguishes staff materials from the right-hand skin. Blender checks closed components, zero degenerate faces and positive summed volume. `geometry-check.json` contains measured results for every LOD and all eight clips; the largest observed normal-length error is below 1.3e−7.

The first proof exposed an incomplete hood covering, long face-print chords cutting through the head, and a crossed strap polygon. The final source holds the hood radius above the cylinder, keeps its crown inside the capsule, divides curved prints into narrow closed strips, and orders the strap perimeter correctly. The retained `proof.png` is the corrected actual asset, not an image-generated concept or a game screenshot.

Rebuild from the repository root using **new** paths:

```sh
/snap/blender/current/blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/adventure_assets/generate_raider.py -- author --output-dir build-adventure-c/raider-rebuild-r01
python3 tools/adventure_assets/generate_raider.py check --package build-adventure-c/raider-rebuild-r01 --output build-adventure-c/raider-rebuild-r01/geometry-check.json
python3 tools/adventure_assets/generate_raider.py cook --tool bazel-bin/tools/gltf_vmesh_tool --source-dir build-adventure-c/raider-rebuild-r01 --output-dir build-adventure-c/raider-rebuilt-package-r01
python3 tools/adventure_assets/generate_raider.py check --package build-adventure-c/raider-rebuilt-package-r01 --output build-adventure-c/raider-rebuilt-package-r01/geometry-check.json
/snap/blender/current/blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/adventure_assets/generate_raider.py -- proof --package build-adventure-c/raider-rebuilt-package-r01 --output build-adventure-c/raider-rebuilt-package-r01/proof.png
```

The output parent `build-adventure-c` must exist. Authoring used Blender 5.2.1 LTS. In this host's sandbox, Blender completed the saved outputs and completion marker but stalled in PulseAudio shutdown; the owned process was interrupted afterward. Geometry checks and all three strict cooks exited successfully. A `.blend` container regenerated by Blender need not be byte-identical; admission always uses the exact hashes of the resulting source and cooked files.

Asset checks do not claim renderer integration, enemy gameplay, frame time or encounter completion. Those require the separate runtime validation.
