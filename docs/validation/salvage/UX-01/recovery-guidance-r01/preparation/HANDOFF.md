# D41 native first-recovery guidance — handoff

Status: source-only candidate. No C++ build, CPU/GPU test, app, screenshot, runtime acceptance or completed gate is claimed. Live sources, parked two-job work and assets were not changed. Root owns applying, testing and publishing this candidate before the 2026-09-12 07:27:33 UTC stop.

## Install

Apply `candidate.patch` against the current checkpoint, preserving any unrelated later Application edits. Do not overwrite the whole Application file blindly. `base-sha256.json` identifies the combined mechanism/dock baseline after the root-requested rope adapter and F9 shortcut rebases; `base-rebase.json` preserves the preceding Application hashes and exact correction; `candidate-sha256.json` pins this overlay. Six files change: Application, the existing HUD header/build entry, the new header-only formatter, existing HUD tests, and the optional native delivery driver. No CMake source addition is required for a header-only helper. Existing render dependencies already cover it.

## Behavior and authoritative inputs

`src/render/cove_recovery_guidance.hpp` contains pure `CoveRecoveryFacts` → `CoveRecoveryGuidance` and `CovePauseFacts` → existing HUD content. Application samples actual session/command guards and observations at the existing 10 Hz HUD cadence. The helper cannot submit commands or modify gameplay.

- J works on foot and requires exact Available job plus current action/global admission guards. H uses exact job/cargo IDs, Accepted phase, storage readiness, all action/global guards, and the existing `preparation.eligible` callback. Numeric explanations cannot grant H by themselves.
- Delivery explanations use the current cargo root, not the boat speed: horizontal distance plus the unchanged 1.1 m cargo allowance, root height, full 3D translational speed and angular speed. Current profile values are radius 5 m (usable root distance 3.9 m), height −1 m, speed 0.8 m/s, spin 1 rad/s. There is no invented requirement to use helm/tow or lift the entire cargo above water.
- Hook approach uses the real 8 m endpoint range. Winch controls require the actual player/root/body/action guards. An attached tow must have a matching observed handle, accepted change tick and event completion before displaying reel/return instructions. Detached cargo that is already in the delivery zone can settle/deliver without adding a rope.
- H admission first shows securing; banked cargo awaiting its archive shows saving. Delivery success uses `deliveryDurable`. Harbor power requires actual off-boat 3D range ≤4 m, valid boat, all K guards, ready storage, and `Stage::Absent` with profile zero. K starts preparation; installed geometry alone is not durable power.
- Paused harbor/rescue/checkpoint work outranks historical delivery. No Resume hint appears during installation before its checkpoint exists. Current manual `Saving ` or recoverable `Save failed.` status outranks historical success. Revoked storage asks for restart and does not offer F10. Actual parking/space refusal remains visible. Existing workshop/build/paint branch stays authoritative.
- Nearby E, movement, B workshop, P pause and R rescue remain visible in normal play. Pending work displays waiting controls. The existing 768 quads, 20 px minimum body text, atlas, shaders and 167,936 requested HUD bytes remain unchanged.

`nativeHud.objective`, `title`, `selected`, `status` and `hints` expose the renderer's existing cached content, never a new observer-time decision. `lastEncodedQuads` keeps its existing encoding-only meaning; it is not a completed-HUD counter. The driver separately waits for actual scene/physics completion for its captured observations.

## Focused checks authored, not run

Run the existing `//tests:cove_hud` target with `--test_filter=CoveRecoveryGuidance.*:CoveHud.ReadableBoundsKeepTheBuildAndPaletteClear:CoveHud.OversizedTextAndTinyWindowsStayBoundedWithoutShrinking` after integration. The six new cases cover:

1. J on foot; H requires authoritative permission, with no mandatory winch/rope.
2. Inclusive range/height/speed/spin boundaries and invalid measurements.
3. Missing cargo, winch/root or confirmed tow cannot advertise ready Hook.
4. Banked/installed state cannot claim durable delivery/power.
5. Both independently found paused-state regressions, manual save progress/failure, refusal, revocation and retained economy; includes 640×480/960×540 layouts.
6. Every running mission step fits the same layouts and unchanged GPU limits.

Both native/browser app builds remain necessary for the shared header/build wiring. The previously passed GPU HUD/shadow/mechanism cases need no repeat solely for text changes. Follow the repository's mandatory commit checks.

## One actual native journey

After integration/build, use fresh output/storage directories:

```sh
python3 scripts/validate_native_cove_delivery.py --binary <final-native-binary> --storage-root <new-storage-root> --output <new-output-root> --objectives-and-harbor
```

This opt-in mode extends the existing real generator haul. It builds the same paid 1,035 kg lifting rig, accepts J, follows the marked dock turn, boards/uses the helm through real prompts, hooks and reels actual cargo, returns/hoists, delivers H, waits for durable disk publication, terminates/restarts, checks exact paid IDs/inventory/mass and no duplicate reward, then uses the existing real Rescue-to-berth route and K to install/persist harbor power. It stops after power; it does not repeat sailing, the eight-brick journey, camera matrices or mechanism motion checks. No profile or save payload is injected.

Expected final material is 96 with the same paid part ID, banked generator and durable harbor. Both payloads are preserved. Every recorded state in this mode requires readable encoded HUD content, current seven-part presentation/shadows, and later completion of the captured GPU serial/owned physics tick. Body identity must remain exact during each completion proof. The driver is bounded to eight minutes plus process cleanup. Process logs are checked for unexpected GPU/runtime failures; outputs and failures are preserved.

`--permission-failure` remains a separate optional flag. It can extend that same haul with the existing real unwritable-root failure/F10 retry, and waits for the 10 Hz HUD to show the actual failure before asserting text. The base acceptance does not require this extra failure path. Historical invocations without `--objectives-and-harbor` keep their old flow.

A successful path may cross securing/saving between HUD samples. Do not delay gameplay or require a transient screen solely for observation. Their semantic behavior is covered by the focused CPU cases; the stable optional failure path checks retry display if requested.

## Source-only audit

Python syntax parsing passed. Baseline copies match their recorded SHA-256s. The one-line root-requested rope adapter correction is applied equally to scratch base and overlay, so this patch preserves the current mechanism fix. The subsequent exact root F9 relocation before Cove/workshop guards is also applied to both trees and excluded from the D41 patch. Static title widths were computed from the committed font advance table: every title fits the 248 px inner width at 640×480, and the nearest-to-limit approach title was shortened. Whitespace checking passed. This is source verification, not execution of the authored tests. Independent reviewers found and corrected paused installation/manual-save precedence and prompt-based walking/10 Hz failure sampling; their final reports belong beside this handoff.
