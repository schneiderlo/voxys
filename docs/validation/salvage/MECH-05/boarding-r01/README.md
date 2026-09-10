# Correct water placement for Rescue and Launch

This fixes the reproduced paid-boat boarding failure from `root-recovery-r01`.
The underlying problem was the initial water-height estimate, not the boarding
reach rule. Parent fragment/cutting tasks and game gates remain open.

## Diagnosis

The previous and new atomic-recovery builds both failed the same actual native
journey with the same 13-part save. Rescue/save/restart succeeded, but the boat
received a large upward kick and drifted out of boarding reach. The failed
comparisons remain in `../root-recovery-r01/native-rescue-r01/` and
`../root-recovery-r01/native-rescue-before/`.

An actual GPU compute/readback probe sampled the shipping FFT texture at world
`(-19.5, -91)`, using the physics shader's UV convention and bilinear sampler.
At wave time 0.383333 seconds and strength 0.1:

| Method | Surface offset |
|---|---:|
| Previous 24-mode CPU estimate | −0.541274 m |
| Actual GPU spectrum plus analytical swells | +0.213381 m |
| All modes with the previous spatial origin | −1.15125 m |
| Corrected origin, still only 24 modes | −0.288991 m |
| Corrected complete-spectrum placement query | +0.213541 m |

The old estimate put this recovered hull about 75 cm too deep. Two distinct
errors mattered: the CPU spectral coordinates did not use the GPU's half-patch
origin, and 24 dominant modes omitted too much of this particular wave field.
Changing just one of these did not fix the estimate.

## Implementation

- The ordinary CPU approximation now uses the correct spatial origin. Its
  temporal cache and per-frame evaluation still use only 24 modes.
- `WaterSimulation::samplePlacementHeight` retains the complete bounded
  spectrum, evaluates the eight texels required by the two cascades, rounds
  each to the shipping half-float texture format, and applies bilinear filtering
  and the shared analytical swells. Self-mirroring frequency bins count once.
- The extra retained mode data is about 3 MiB. The full calculation runs for
  occasional placement, not for each body on each tick. This is not a general
  WaterField performance or approximation-bound acceptance claim.
- Spawn, workshop Launch and Rescue use this query. Invalid/unavailable results
  refuse placement. Loading still restores actual saved motion; the correction
  does not rewrite old archives or change their supported schemas.
- Reach distances, standing positions, collision shapes, movement controls,
  boat parts, inventory, wave strength and GPU forces are unchanged.

## Verification

- All five `water_simulation` tests pass. The new GPU comparison covers 40
  combinations: two spectra, five positions including repeat boundaries, and
  four phases including 4095.9 seconds. Every complete-spectrum placement
  sample agrees with actual GPU output within 5 mm. Invalid input, uninitialized
  sampling and zero strength are also checked.
- The **unchanged** native failed-journey driver now passes all eight stages:
  initial restore, Rescue/save/restart, boarding and real generator hooking,
  reeling, another Rescue/save/restart, then boarding and sailing.
  Paid IDs 35/100, all 13 parts, zero material and the unique generator survive.
  The first post-Rescue speed drops from 1.2483 m/s in the failed run to
  0.04532 m/s; after restart it is 0.08730 m/s rather than 2.44066 m/s.
  Final sailing reaches 2.18448 m/s over actual completed physics ticks.
- Bazel native, CMake native and WASM application builds pass.
- Browser `browser-rescue-r02/` passes all six recorded stages and drained Leave:
  two Rescues, two real save/reloads, boarding, Helm and sailing. The 13 parts,
  paid IDs 35/100, zero material and accepted unbanked generator remain intact.
  `browserErrors` is empty. The same frozen application is used for both browser
  runs. No screenshot or visual-diagnostic loop was used.

`checks/` contains retained diagnostic outputs, final test XML and build logs.
The initial GPU probe failed to compile because its test omitted the vec4
header; the corrected probe then produced the measurements above. The interim
all-mode and origin-only experiments are diagnostic evidence, not shipped
alternatives. No warning was disabled.

Browser r01 also completed both Rescues/reloads and boarding/sailing, but its
runner failed during Leave: the page navigated and destroyed `voxyModule` before
the next old-owner poll. The UI navigates only after observing `active:false`.
The corrected driver waits for that normal `experience=lego-world` destination,
as the existing save driver already does. R01 remains a failed full run; r02 is
the complete passing rerun with this driver correction. Gameplay code and
acceptance thresholds did not change between those browser runs.

## Reproduction

Use the project Nix toolchain from the repository root. GPU jobs run sequentially.
Create fresh test storage/output directories and preserve existing saves.

```sh
bazel test -c opt //tests:water_simulation --test_output=errors
python3 scripts/validate_native_cove_rescue.py --binary <frozen-native> --source-slot build-cove-root-resume-_csjbh9y/native-saves-r01/c708a658908c35223b4ed6be5999becf --storage-root <new-root> --output <new-report> --hook-before-second-rescue
```

Scratch: `build-cove-boarding-esi6q6gq`. Frozen native binary:
`native-boarding-r01`; frozen browser package: `web-r01` within that scratch.
Browser test profile: `/tmp/voxys-startup-jLGqhu`, origin port 41289, world
`00a297a0ef96a7134f245d2d48080acf`. The full browser driver is
`scripts/smoke_integrated_wasm.mjs <frozen-web> salvage-cove` with
`VOXY_SMOKE_COVE_RESCUE=<new-report>`, the corresponding profile/port/resume-world,
`VOXY_SMOKE_GPU=gaming-x11`, and `VOXY_SMOKE_NO_SCREENSHOT=1`.
Do not enable the checkpoint-only mode: this regression requires boarding and
sailing. The owner's port-38206 preview remains untouched.

Next: protect the last intact design before cutting, complete child-resource
reservation/rollback and actual multi-root cut publication, then verify native
and browser cut/reload/distant recovery and repeated rebuilding. Keep unfinished
MECH-05/PLAY-05 and gates unchecked; no gate commit is due for this scoped fix.
