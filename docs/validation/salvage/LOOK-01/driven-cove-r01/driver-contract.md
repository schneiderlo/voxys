# One native and one browser journey — verified scope

The earlier native r06 machinery journey passed 39 stages and 11 phase checks
in 68.056 seconds on frozen mechanisms/native-r04. The final combined browser
r02 passed 28 machinery stages in 84.663 seconds after five separate objective
stages in 3.033 seconds on guidance/web-r01. Final guidance/native-r01 completed
a separate 42-stage recovery/save/restart/power journey. Exact package/source
identities, earlier failures and pending mandatory suite are in README.md.
No screenshots or camera matrix were used. The following remains the reproduction
contract; it is not an instruction to repeat unchanged successful journeys.

## Actual controls and expected state

1. In browser, physically press F9 and confirm the existing read-only uncapped
   status; repeat after reload. Native uses its existing --uncapped option.
   Fresh Cove must contain 11 boat parts, mass 1035 kg, 48 salvage material,
   zero special machinery, no paid part IDs and exactly seven presentations.
   Walk the marked turn route through `(6,-49.5)` and `(4.5,-49.5)`, then end
   the boarding approach when the real board prompt is available. Physically
   use it and confirm boarding; approach/use the helm in the same way.
2. Hold W and S in separate stable six-submitted-tick windows. Confirm the
   corresponding positive/negative rotor phase, then release and prove stop.
3. Return through the real boat/dock interaction. Select Propeller through Next
   in Workshop, choose Reverse (native N / browser Reverse), Keep and Launch.
   Quote and inventory change must be zero. Board; W must now reverse rotation.
4. Dock again and disable Propeller (native X / browser On/Off), Keep/Launch for
   free. Board; W must produce zero effective drive and a frozen rotor. Keep
   this deliberately disabled-and-reversed design through save/restart.
5. Hook the actual generator (F), reel (Q) for eight accepted rope ticks and pay
   out (Z) for three. If their accepted displacement cancels the final drum phase,
   apply one extra real Q pulse and verify its length/phase relation, then release.
   Require signed length/phase agreement and a nonzero final drum phase. This does not repeat cargo delivery.
6. Return to Workshop; phases freeze and animated-part count becomes zero.
   Close, physically pause and save once. Paused phases must freeze while
   completed frames continue. The exact paid IDs, stock, mass and design persist.
7. Reload the physical save. Ephemeral phases, effective drive and rope sample
   are neutral. Resume/open Workshop and verify the exact persisted disabled/
   reversed design and unchanged ownership. Native compares archived full
   part/connection/recovery blueprint content; browser compares exported kept
   blueprint bytes through the existing read-only export action.

Both drivers include the normal dock check in that same sequence:
`0 < dockMarkingGpuBytes <= 16384`, total fixture reservation exactly
`10745384 + dockMarkingGpuBytes`, two submitted marking draws outside Workshop
and zero inside. The new route is the actual original spawn-to-boarding route;
no teleport/private mutation is allowed to establish success.

The first native attempt failed because it demanded an exact final waypoint
even after the board prompt appeared. Both drivers now end board/helm/dock
approaches on their real prompts, retaining physical E/click and confirmed
transitions. This is a driver correction, with no app/physics/camera changes;
the failed attempt remains in `native/attempt-r01/`.

The second native attempt passed forward/reverse/stop pairs, then repeated the
exact-waypoint mistake on the dock-to-Workshop approach. That approach now ends
when `workshop.canOpen` is true, followed by physical B/click and confirmed open
state. `native/attempt-r02/` preserves the failure, including its separate
surface-acquisition error. A separate named-Timeout source correction preserves
genuine errors and the scanners. The third attempt recorded a timeout followed
by successful reacquisition and no process errors, but then failed a repeated
approach after its successful reversed-propeller refit. None is a completed journey.

Movement helpers now wait for a newer player-tick observation after releasing
keys, matching the existing delivery helper instead of reusing the same 10 Hz
position. Every boarding intermediate step honors the real board prompt. A
bounded native `failedApproach` trace records future movement failure. These
changes were candidate driver fixes; actual r04 completed those approaches and
both refits, with no physics/camera mutation or success injection. It then
failed the real reel/drum phase comparison. That failure is preserved under
`native/attempt-r04/`; the Application correction and next integration journey
must satisfy the same unchanged phase/identity/completion assertions.

## Completion and motion proof

`boat.mechanisms` exposes tick/incarnation strings, rotor/drum radians,
effectiveDrive, animatedParts, bodyIndex/bodyGeneration, and ropeTick/
ropeIndex/ropeGeneration/ropeLength. There must be two animated parts outside
Workshop and zero inside. Finite phases remain in `[0, 2π)`.

For captured state A, a later observation must prove
`fixture.completedSerial >= A.fixture.submittedSerial` and
`physics.completed >= A.mechanisms.tick`, while incarnation, body generation,
build identity/topology and root keys remain unchanged. Every recorded state
stores its completion proof. Submission or an advanced wall clock alone is
insufficient.

Stable drive pairs check wrapped residual
`Δrotor − 4π × effectiveDrive × ΔsubmittedTicks / 60`. Rope pairs require the
same valid generational handle and a newer accepted rope tick, then check
`Δdrum + ΔrestLength / .28`, with directional length change greater than 2 cm.
Tolerance is 0.0003 radians; freeze tolerance is 0.000001. Nonzero expected
motion requires `abs(sin(expectedAngle)) > .1` to reject whole-/half-turn
aliasing. Endpoints are captured and completion-proven before releasing input.
All held keys release on failure paths. Browser uncaptured errors and native
stopped-process logs are checked; the outer smoke report separately records
browser errors.

## Commands after integration

Run from repository root with the active host DISPLAY/XAUTHORITY and fresh
directories. These commands are reproduction instructions, **not executed
results**. Replace the package/binary path if the final packaging differs.

```sh
nix-shell --run 'python3 scripts/validate_native_cove_mechanisms.py --binary build-cove-guidance-r01/native-r01/voxy_native --output build-driven-recheck/native --storage-root build-driven-recheck/storage'
nix-shell --run 'VOXY_SMOKE_GPU=gaming-x11 VOXY_TEST_CHROME=/opt/google/chrome/chrome VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 VOXY_SMOKE_TIMEOUT_MS=300000 VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_PRESENTATION_PARTS=7 VOXY_SMOKE_COVE_MECHANISMS=build-driven-recheck/browser VOXY_SMOKE_REPORT=build-driven-recheck/browser-startup.json VOXY_SMOKE_KEEP_PROFILE=1 node scripts/smoke_integrated_wasm.mjs build-cove-guidance-r01/web-r01 salvage-cove'
```

The native driver expects exactly seven presentations. Browser defaults to
seven and accepts an explicit bounded expectation, not any observed count.
Copy current `web/` and freshly built `.js`, `.wasm`, `.data` to the named
package. Record their hashes with the reports. Required outputs are native and
browser journey `summary.json`, physical save/proof artifacts, native process
logs and the outer browser startup report, all with actual results and errors.

Focused test filters are preserved exactly in `checks/*-test.log`. Gameplay,
prefab and owner checks use `//tests:voxy_tests`; the standalone numeric pose case
uses `//tests:mesh_path_test` with
`MeshPathGPUTest.NamedMechanismsKeepStationaryNodesAndMatchLiveBodyAtLargeSectors`.
Dock adds `CoveDockMarkings.*` and
`FixtureGPU.DockMarkingsUseExactOwnerChargeSharedDrawTicketAndRetirement`.
Use the repository Nix/Bazel environment, `--local_test_jobs=1 --test_output=all`,
and leave `VOXY_MESH_CAPTURE_DIR` unset. Root coordinates GPU/build ownership
and the final required suite. Do not repeat unchanged journeys for more views.
