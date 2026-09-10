# Atomic section recovery

Scoped D32 implementation. Rescue now reserves one complete move for all live
boat sections and the unbanked generator. Native and browser ordinary Rescue
save/reload checks pass. Actual multi-root application cut/recovery and the parent
tasks/gates remain open. The wider-boat boarding defect recorded below was later
fixed in [boarding-r01](../boarding-r01/README.md); retain the original failures
here as evidence. No screenshots or gate commit.

## Implemented behavior

- `CoveRigidRoots::prepareRecovery` requires every owned section at the same
  completed tick. It rejects stale observations, aliased body slots, mismatched
  assemblies, invalid motion, unsupported bounds and capacity failures without
  moving anything or changing ownership.
- The berth uses the mass and deduplicated displacement of the whole accepted
  layout. Each section retains its own root anchor and mass frame. Separating
  overlapping buoyancy sources cannot count that volume twice. A nonfloating
  layout starts with its lowest collision point 5 cm above the water; it may
  subsequently sink. Recovery grants no flotation, support, parts or weld repair.
- Each section receives an upright teleport and zero linear/angular velocity.
  These commands and the unbanked generator's commands share one checked,
  joined-boundary physics mutation. Insufficient capacity or an invalid body
  refuses the complete batch. Rope release is an earlier intentional phase.
- Resetting the player/view no longer independently queues primary-body moves.
  Target publication follows successful commit. Saving waits for actual common
  observations of every returned section and unbanked cargo, including pose checks.
  Gravity and water may add motion during the neutral observation tick.
- Paid IDs, inventory, accepted/disabled bonds, job state and the unique cargo
  are unchanged. Banked cargo stays at its secured position.

## Executed checks

| Check | Result |
|---|---|
| CPU cove selection | 41 cases pass in `checks/player-cpu-r03.xml`. |
| Paid-layout berth check | One additional CPU case passes in `checks/paid-berth-r01.xml`: the two-purchase layout does not intersect authored dock boxes at its flat-water equilibrium. This does not certify wave forces or terrain clearance. |
| Actual GPU recovery | Two real authored sections, initially more than 10 km from home, remain remote and moving after full-queue refusal, stale-body refusal and explicit discard. A valid six-command commit returns both on the next observed tick, upright with zero velocities in the gravity/water-disabled fixture. The case passes in `checks/recovery-gpu-r02.log`; that combined invocation failed only in the separate CPU fixture described below. |
| Native delivered boat | All 7 stages pass in `native-banked-r01/`: two Rescues, two process restarts and sailing at 2.18413 m/s. Eleven parts, paid ID 35, 96 material and the secured generator survive. |
| Browser live cargo | All 6 recorded checkpoint stages plus drained Leave pass in `browser-checkpoint-r01/`. The saved 13-part boat starts with an actual attached cable. Rescue releases it and keeps paid IDs 35/100, zero material, the accepted job and one unbanked generator. Save ticks 139/144 reload at 140/145; all section observations join. `browserErrors` is empty. |
| Application builds | Bazel native, CMake native and WASM pass. `git diff --check` passes. |

The eleven-root CPU preparation case cuts every starter weld, preserves every
part/disabled bond and prepares 33 commands. It uses explicit synthetic handles
only to test host validation; the GPU case obtains real admitted handles and
observations. These are not actual multi-root application cut/reload journeys.

The browser checkpoint scope deliberately excludes boarding/sailing. Its saved
source is the prior real towing journey in isolated profile
`/tmp/voxys-startup-jLGqhu`, origin port 41289, world
`00a297a0ef96a7134f245d2d48080acf`. This test advanced that test save to a rescued
checkpoint. The owner's preview on port 38206 was not changed.

## Open boarding defect and retained failures

`native-rescue-r01` restores the current 13-part paid save, completes Rescue and
restart, then fails to board: the boat has drifted beyond the dock interaction's
reach. `native-rescue-before` repeats the identical save and controls with the
previous frozen application, `build-cove-root-resume-_csjbh9y/native-resume-r01`.
It fails at the same step. The first recovered positions are identical in the
reported precision. Thus the defect predates this atomic-move change. Both failed
runs remain failed; neither is represented as a complete journey pass.

At the failed observations the player remains near `(4.5, 1.285, -52.9)` and the
boat root is around `(-2.63, 1.4, -56.74)` in cove coordinates. Initial recovery
is near `(-0.5, -0.135, -54)`, with substantial upward acceleration during the
following ticks. The cause of the subsequent motion is **not established**.
Check actual GPU water/terrain/contact response and boarding reach before full
recovery acceptance. Do not increase boarding distance or label the failed run
passed merely to avoid this defect.

Earlier preparation tests incorrectly assumed that the isolated helm had no
useful displacement. Its real metadata contains 135.68 kg of displacement
capacity for 30 kg dry mass. The corrected fixture checks the actual requirement:
every original part is its own root and the whole layout retains the intact
berth calculation. A separate GTest dangling-else compile error was corrected
with braces. Original failed logs are retained; no warnings were disabled.

## Reproduction and next implementation

Use the Nix toolchain from the repository root and new output/storage paths.
Frozen runtime: `build-cove-root-recovery-xgzzff3s/native-recovery-r01` and
`build-cove-root-recovery-xgzzff3s/web-r01`. The hash manifest in `checks/`
identifies source, drivers, frozen artifacts and previous comparison binary.

```sh
bazel test -c opt //tests:cove_player --test_filter='CoveMovement.Recovery*:CoveMovement.ActualRecovery*' --test_output=errors
python3 scripts/validate_native_cove_rescue.py --binary <frozen-native> --source-slot <real-save-slot> --storage-root <new-root> --output <new-report>
```

The native delivered source is
`build-cove-root-resume-_csjbh9y/native-upgrade-saves-r01/614fcf5f359b597c1248034c82f3b9e0`.
The failed wider-boat source is
`build-cove-root-resume-_csjbh9y/native-saves-r01/c708a658908c35223b4ed6be5999becf`;
use `--hook-before-second-rescue` for that unchanged comparison.

For a fresh browser checkpoint test, start from a separate real attached-cargo
save. Use `scripts/smoke_integrated_wasm.mjs <frozen-web> salvage-cove`,
`VOXY_SMOKE_COVE_RESCUE=<report>`, `VOXY_SMOKE_COVE_RESCUE_CHECKPOINT_ONLY=1`,
the matching saved profile/origin/world, `VOXY_SMOKE_GPU=gaming-x11` and
`VOXY_SMOKE_NO_SCREENSHOT=1`. The focused driver requires an attached unbanked
source. Its default full journey still includes boarding/sailing. The shared
click helper now uses the already-proven stable-target wait after scrolling.

Next: resolve the wider-boat boarding defect; protect the last intact design
before the first cut; reserve and roll back every child body/water resource;
publish actual cuts and their consumers atomically; execute native/browser
multi-root cut, save/reload, distant recovery and repeated rebuild journeys.
Cut controls remain disabled. Parent MECH-05/PLAY-05 and all unmet gates stay open.
