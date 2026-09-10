# Native delivery, save failure and restart

Scoped PLAY-04/SAVE-04 validation, on the authorized implementation tree at
HEAD `7f28fabfabf3d63726f6cfa1001c3ce1ed557911`. The permission-failure/retry
journey r07 and ordinary automatic-save journey r08 both pass. Exact source,
binary and evidence hashes are in `manifest.json`.
No full parent task, game gate, independent review or gate commit is claimed.

## Implementation and real controls

The existing native delivery host is exercised through actual X11 key events
and the normal game loop. It secures cargo, joins the completed Pause boundary,
publishes the Banked SVCE through the existing disk worker, then acknowledges
the exact captured digest before releasing the durable reward and Resume.
No storage schema, physics, asset, delivery threshold or terrain change was
made for these checks. The earlier browser delivery evidence is retained at
`../delivery-r01/`; it was not rerun for this native-only observation change.

`--expedition-observe /absolute/new-directory` adds optional read-only state
observation to the native host. It uses the same application getter as browser
validation, with no additional GPU readbacks, input actions or state setters.
At most ten samples per second replace `state.json`; each is bounded to 64 KiB,
with at most one staging file. A refused/failed observer cannot acknowledge
storage or alter game authority. Normal launches perform none of this I/O.
The output is diagnostic evidence, not a checkpoint or performance measurement.

The driver selects only the owned child window by `_NET_WM_PID`. It does not
change desktop focus or capture images. It reads GLFW's actual physical key
scancodes, avoiding layout-dependent Q/W/A/Z swaps on AZERTY. Construction waits
for actual selected-part, placement and accepted-revision observations before
the next input. Movement/reeling holds wait for observed game steps.

The boat is built through the same paid workshop as the browser case: remove
the loan cradle, buy one 12-material beam and move the winch out over the water.
Exact placements are in the driver and the previous delivery report. No free
parts, privileged actions or success injection are used. The changed craft
has eleven parts, mass 1,035 kg and paid part ID 35; stock becomes 36.

The final native approach differs from the earlier browser route: shorten the
line to about 4 m, tow the partly submerged generator inside the harbor, then
hoist until the real Deliver condition passes. This retains hull clearance on
the loaded return. The harbor's existing minimum cargo-root height stays -1 m;
the driver does not redefine the handoff height, rope strength or physics.

## Verified permission-failure journey

r07: **40 stages passed**, two actual game processes, one saved-world restart.
Native hardware Vulkan reports AMD Radeon 890M Graphics (RADV STRIX1). The X11
window is 960×540 with the shipped `--uncapped` option and the diagnostic
observer enabled. This proves control/state behavior, not visible performance,
visual quality, VSync performance or stability of every craft/approach.

Storage is isolated under `build-native-haul-nvbpgiu0/saves-r07` on the
workspace's ext4 filesystem, `/dev/nvme0n1p6`. No user saves were touched.
World: `041f6bbd6d99d5d8f2db86be3c30de7c`.

| Step | Observed result |
| --- | --- |
| Paid build and loaded return | 11 parts, 1,035 kg, 36 material; cable intact |
| Cargo brought inside harbor | Root `[-2.48466,-1.95934,-54.4398]`, distance 3.01688 m, rope 3.8174 m |
| Hoist and admit Deliver | Root `[-2.0472,-.947563,-53.7896]`, distance 2.55587 m; actual eligibility true |
| Root made unwritable, press H | Physical and logical delivery complete; save fails with real filesystem permission error; pending=true, durable=false; tick 2351 |
| Attempt P/R/H/W while unsaved | Pause state, water time and player unchanged; no durable acknowledgment; no current save file |
| Restore permission, press F10 | Mirrored generation 1 committed; pending=false, durable=true; 96 material; same tick 2351 |
| Terminate process, reopen world | Same paid ID, boat mass, secured static cargo and 96 material; tick 2352, epoch 2 |
| Resume and attempt H again | No new cargo and no extra reward |
| Sail away | 2.16885 m/s boat speed; secured cargo position unchanged; 96 material |

The failure uses actual mode 0500 on the new isolated save root, without mocked
storage results. The native title reports the real write-permission failure.
The driver restores mode 0700 before retry and during cleanup. The in-memory
96-material value during failure is explicitly not a durable receipt: the game
stays frozen and the success flag remains false until the retry publishes.

`delivery.svce` retains the actual 14,547-byte payload at tick 2351. SHA-256:
`ed58500bbcb7cc54ced9f4b6d9f6b8701d9b8fc70c38fb6709b64d82f8a11fa3`.
The driver independently checks identical current/mirror SVSG bytes, outer and
inner checksums, world, generation and tick. Recovery publishes the fresh-owner
and retired-parent archive before admitting its one neutral initialization tick.
The process is terminated after successful publication. This is not power-loss,
mid-write crash, full-disk or post-rename uncertainty certification.

## Builds, refusals and reproduction

The normal automatic-save journey **r08 passes all 34 stages**, including its
own process restart. World `a76a1221672cefbb385025e1f5f608e3` banks and saves
automatically at tick 2112 / epoch 1, without F10 or a permission change. The
game reopens at tick 2113 / epoch 2 with the same 1,035-kg craft, paid ID 35,
secured static generator and 96 material. Repeated H pays nothing. Sailing
afterward reaches 2.25001 m/s with the generator position unchanged. The actual
generation-1 payload is 14,547 bytes; SHA-256
`bce9910708ab9e15873785c752fd3c67af779c33550e97ceba9430c1cb0a0808`.
Its files are in `journey-r08/`; the separate save root is
`build-native-haul-nvbpgiu0/saves-r08`, also on workspace ext4.

Bazel and CMake native application builds pass. Three actual command-line
refusals pass before game initialization: relative observation path, duplicated
observation argument, and a directory that already exists. Logs and exact
arguments are retained. Shared gameplay/physics/save code is unchanged from the
previous 264-case native and twelve-coordinator/sixteen-UI checkpoint; those
earlier test counts are historical, not newly executed suites in this record.

Use new save/output directories. The driver refuses to reuse them:

```sh
nix-shell
bazel build -c opt //:voxy_native
cmake --build build-native-save-host --target voxy_native -j8
python3 scripts/validate_native_cove_delivery.py \
  --permission-failure \
  --storage-root /absolute/fresh/save-root \
  --output /absolute/fresh/evidence-directory
```

Omit `--permission-failure` for the ordinary automatic-save path. The driver
adds the cove configuration, 960×540, `--uncapped`, an explicit new observation
directory per process and X11 selection. It sends only normal player keys.
Leave all failed runs intact; do not silently replace their summaries.

## Retained failures and limits

- r01 advanced selection before the prior native observation refreshed and
  removed the wrong part. Explicit selection/revision waits correct the driver.
- r02 used keysym-based Q lookup on an AZERTY desktop, which sends the physical
  A action instead of the game's Q action. Reading GLFW scancodes corrects this.
- r03's short wall-clock movement presses occurred between slow native frames;
  the player never moved. Movement now waits for actual game steps. Subsequent
  runs also use the normal uncapped option. VSync/visible-performance acceptance
  remains open; the source of the slow displayed cadence was not certified.
- r04 reached the helm but did not hook. The corrected driver waits for actual
  reach with margin and holds the Hook key across observed steps. The old run
  did not retain the exact rejected input-time range, so its cause is not claimed
  to be proven as solely input loss or solely distance.
- r05 wound the raised load into the boat and overturned it. r06 stopped sooner
  but its high-load return still lost lifting height and failed the harbor gate.
  These remain actual limitations of that approach, not dismissed GPU failures.
  The successful approach tows partly submerged and hoists inside the harbor.
- Native controls/diagnostic sampling and platform pacing differ from browser
  automation. These runs do not certify general machine stability, fixed-tick
  WaterField, controller support or the final human onboarding experience.

## Next work

Implement the durable generator-powered harbor lift and useful capability
unlock, followed by rescue and the second job. Reconstruct upgrades
from validated durable progress after restart; never expose unearned upgrades
while delivery saving is pending. Preserve actual cargo motion and ownership.

Native/browser mid-publication faults, journal/receipt compaction, world picker,
export/import, Windows storage, latching, general stability, fixed-tick water,
independent reviews and full game/release gates remain open. Preserve the LEGO
terrain and D20's prohibition on repetitive screenshot loops. Commit only when
a full gate and the required repository checks pass.
