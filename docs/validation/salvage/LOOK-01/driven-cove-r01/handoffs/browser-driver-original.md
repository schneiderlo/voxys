# Browser mechanism journey — prepared, not executed

The isolated browser driver and smoke dispatch are ready. **No browser/game,
GPU workload or screenshot has run for this change.** Integrate only after the
current paint checkpoint is finished and the renderer/Application are accepted.
Use new package, profile and result directories. Session work stops at
2026-09-12 07:27:33 UTC.

Files: `overlay/scripts/validate_cove_mechanisms.mjs` and
`overlay/scripts/smoke_integrated_wasm.mjs`; the unchanged original wrapper is
retained in `base/scripts/smoke_integrated_wasm.mjs`. Only the new
`VOXY_SMOKE_COVE_MECHANISMS` dispatch is added. The driver defaults to exactly
seven presentations; `VOXY_SMOKE_PRESENTATION_PARTS` can explicitly override
that expectation within the existing 1–16 fixture bound.

## One bounded actual-control sequence

1. Fresh Cove: 11 parts, 1035 kg, 48 salvage material, zero machinery and no
   paid part IDs. Board from the existing berth and walk to/use the helm.
2. Hold W, then S for separate stable six-submitted-tick windows. Observe the
   expected positive/negative rotor phase. Release each key and verify frozen
   phases while completed frames continue.
3. Return to the dock through the actual boat/dock interaction and walk to the
   workshop station. Select Propeller through Next; choose Reverse (the browser
   button corresponding to N), Keep and Launch for zero debit/refund.
4. Board again. W now produces negative effective drive/rotation. Stop, return
   to dock, disable Propeller through the On/Off button (X), Keep/Launch for free.
5. Board a final time. W gives zero effective drive and no rotor advancement.
   The still-enabled winch hooks the real generator; Q reels for eight accepted
   rope ticks, Z pays out for three, and releasing keys stops its motor. Release
   the cable. No lifting/delivery, rescue or additional construction is used.
6. Return to dock, open Workshop and compare the exact kept blueprint. Verify
   existing phases freeze and animated-part count is zero inside Workshop.
   Close, physically pause, verify phases freeze, and save once through the
   actual save control. Drum phase must be nonzero before saving.
7. Reload the actual saved world. Ephemeral rotor/drum phases are zero, with
   neutral effective drive/rope sample. Resume/open Workshop and compare exact
   exported blueprint bytes and disabled+reversed settings. IDs, mass and
   inventory must still match the original ownership contract.

The native driver follows the same substantive controls and assertions. Browser
navigation uses the existing dock/helm waypoints and observed boat quaternion
for the boarding offset. It does not manipulate a camera, teleport, change game
state directly, or reuse the eight-brick paint/construction journey.

## Measurements and completion proof

`boat.mechanisms` supplies tick/incarnation strings, phase radians, effective
drive, animated count, primary body index/generation, and accepted rope
tick/index/generation/length. The driver requires two animated parts outside
Workshop and zero inside, with the exact configured presentation count.

For captured state A, later observations must prove
`fixture.completedSerial >= A.fixture.submittedSerial` and
`physics.completed >= A.mechanisms.tick`. Incarnation, primary body generation,
build identity/topology and root keys must remain unchanged while completion is
proved. Thus encoded/submitted state alone is never credited as completed GPU
work. Every recorded state includes its later completion proof.

Stable-drive pairs check the wrapped residual of
`Δrotor - 4π * effectiveDrive * ΔsubmittedTicks / 60`. Rope pairs require the
same valid handle/generation and newer accepted rope tick, then check
`Δdrum + ΔrestLength / 0.28`. Directional length change must exceed 2 cm.
Nonzero-motion endpoints require `abs(sin(expectedAngle)) > 0.1`. This excludes
whole-turn aliases that hide a frozen mechanism and half-turn aliases that hide
rotation in the wrong direction.
Numeric tolerance is 0.0003 radians; frozen-phase tolerance is 0.000001.
Application emits mechanism numbers with 17 significant digits, so phase near
2π cannot round outside its valid interval and accepted rope deltas retain
their precision. The driver checks uncaptured GPU errors at
each completion proof. The outer smoke runner separately reports browser errors.

All waits share a 300-second journey deadline; held keys are released on every
failure path. The saved final design deliberately retains Propeller disabled
and reversed, so restoring defaults cannot accidentally satisfy the check.
Blueprint export uses the existing read-only export action, only while Workshop
is open. No renderer/private-engine mutation or success injection is used.

## Run only after integration

Stage freshly built `voxy_wasm.js`, `.wasm`, `.data` and current `web/` into a new
package. From repository root, with the host's active DISPLAY/XAUTHORITY:

```sh
nix-shell --run 'VOXY_SMOKE_GPU=gaming-x11 VOXY_TEST_CHROME=/opt/google/chrome/chrome VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 VOXY_SMOKE_TIMEOUT_MS=300000 VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_PRESENTATION_PARTS=7 VOXY_SMOKE_COVE_MECHANISMS=build-cove-mechanisms-r01/browser-journey-r01 VOXY_SMOKE_REPORT=build-cove-mechanisms-r01/browser-startup-r01.json VOXY_SMOKE_KEEP_PROFILE=1 node scripts/smoke_integrated_wasm.mjs build-cove-mechanisms-r01/web-r01 salvage-cove'
```

Expected outputs: the journey summary and outer startup report both say
`passed`, all completion proofs are present, exact blueprint/ownership/settings
survive reload, and browser/uncaptured errors are empty. The command above has
**not run**. Syntax checks only have passed; actual integration results, stage
count, elapsed time, package hashes and raw reports remain pending.
