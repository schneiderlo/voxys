# Robot asset and native controls evidence

The six animation CPU cases pass. The real native journey passes in three
linked processes: **8 ground/camera stages, 10 moving-deck stages, and 3 final
restore stages**. Each process closed on requested SIGTERM, with no force kill
or matching runtime/GPU error lines. No images or screenshots were produced.
The combined browser and mandatory-suite acceptance belongs to the parent
[character record](../README.md).

## What the native checks establish

| Run | Build | Time | Actual controls and evidence |
|---|---|---:|---|
| [Ground](native/native-journey-r01/summary.json) | native r03, SHA `8c7a1ab4…` | 10.497 s | Visible 34-draw robot; walking displacement and advancing clip; jump/fall/land; F2 mode, zoom, load framing, reduced motion and recenter; durable v6 save. |
| [Motion](native/native-motion-r01/summary.json) | Same r03 | 19.230 s | Same saved world; board, helm, drive, leave, jump from moving deck, swim, reboard and rescue; real durable rescue checkpoint. |
| [Restore](native/native-restore-r01/summary.json) | final native r04, SHA `7e39b418…` | 2.201 s | Same v6 world; exact design/inventory, camera choices, position/mode, velocity and facing; later completed paused frames leave player, clip and eye unchanged. |

Every recorded state has a later GPU and physics completion proof tied to its
same incarnation, build, topology, roots and actual body handle. Submission
alone is not counted as completion. Each keeps 11 parts, 1,035 kg, 48 material
and no paid parts. The moving-deck jump preserves horizontal velocity with an
observed error of 0.057958 m/s. Motion is validated through state and completion,
not inferred from a still image.

Ground leaves generation 1 at tick 490. Resuming it republishes a recovery
archive; rescue leaves generation 3 at tick 1503. Final restore republishes
generation 4 through the existing resume protocol. [The final read-only archive
check](checks/final-native-storage.json) confirms exact owned-design/inventory
equality against the motion checkpoint. The [retained final envelopes](native/final-storage/current)
and phase `.svce` payloads are evidence copies, not replacements injected into
the application.

The final build adds the paused semantic-clip initialization and oriented-cargo
restore checks after the first two runs. Only restore was repeated on that
build. The restore-only driver reuses its initial loaded process; it does not
boot the same world twice. Executed driver copies and their exact hashes are
retained in `checks/native-ground-motion-driver.json` and
`checks/native-restore-driver.json`.

## Asset and runtime limits

The installed original robot has three strict cooked LODs, eight real exported
clips, 22 hierarchy nodes, 15 mesh instances and 34 material draws per LOD.
Runtime currently selects LOD0. Its payload is 387,600 GPU bytes; LOD1 is 171,216
and LOD2 114,000. These are selected mesh charges, not the whole fixture owner.
See [accounting](checks/accounting-r04.json), [installed hashes](checks/installed-r04.json)
and the detailed [integration handoff](HANDOFF.md).

The final rounded head reaches 0.299740269 m from the actual capsule segment;
torso reaches 0.277353567 m, within radius 0.30 m. Height is 1.699499965 m and
sampled walk width is 0.588991918 m. Walking extremities reach 0.466877 m
radially. Other clips reach 0.591722772 m in installed LOD0 (swim), or
0.599285920 m across all authored LODs/clips. These extremities do not enlarge
the collision capsule; full limb contact IK is not claimed. [The geometric report](checks/geometry-r04.json)
checks actual normals, vertices, hierarchy and exported keys without previews.

[Final CPU XML](checks/final-cpu-r01-rigid_animation.xml) reports six tests,
zero failures/skips and 69 ms: hierarchy/basis/anchors/bounds, interpolation and
quaternion sign equivalence, outgoing loop policy, malformed admission,
transactional failed sampling, and all real LODs/clips with core clearance.
Headless authoring r07 and all three final strict cooks exit 0.

Earlier failures remain recorded: initial build ordering and strict warnings;
a real 180-degree quaternion tie defect; Blender conversion/polygon/scale
checks; overly wide arms; and the independently discovered head/upper-capsule
clearance defect. The final source fixes those causes. No assertion was relaxed
to hide the head or quaternion defects. [The handoff](HANDOFF.md) separates each
attempt from final acceptance.

## Reproduce the bounded native sequence

Use a host display that the test owns, a built native executable and fresh
output/storage paths. No screenshot option is involved.

```sh
python3 scripts/validate_native_cove_character.py --binary build-native-save-host/bin/voxy_native --output /tmp/actor-ground --storage-root /tmp/actor-storage --phase ground --seconds 180
python3 scripts/validate_native_cove_character.py --binary build-native-save-host/bin/voxy_native --output /tmp/actor-motion --storage-root /tmp/actor-storage --continue-from /tmp/actor-ground/summary.json --phase motion --seconds 180
python3 scripts/validate_native_cove_character.py --binary build-native-save-host/bin/voxy_native --output /tmp/actor-restore --storage-root /tmp/actor-storage --continue-from /tmp/actor-motion/summary.json --phase restore --seconds 90
```

The actual Linux runs used the repository Nix environment, `DISPLAY=:0`,
`VOXY_WINDOW_BACKEND=x11` and the current Xauthority file. Future machines must
use their own valid display credentials. The driver sends real X11 controls,
reads the observer and saves only through ordinary pause/F10 or rescue.
Source/package identities are in [source-manifest.json](source-manifest.json).
