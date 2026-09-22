# Real source wall over a synthetic floor

Isolation fixture: `ImportedWallFlatFloorGpu.ActualSourceWallSettlesOnSyntheticFlatFloor`, in `tests/test_imported_stack.cpp`.

Uses the installed wall metadata's exact 39 source parts at identity orientation/source origin. The original eligibility policy releases 29 anchor-connected parts and leaves the 10 unsupported source parts fixed. The entire imported house remainder is absent. One synthetic floor spans the source bounds plus four units, with its top at the lowest eligible occupied cell (Y=9.9999885559082031). The test uses normal four substeps, 128 body slots, and a 1,200-tick settlement bound.

Result: **FAIL**, 1,200 ticks, phase Falling, 29 dynamic bodies, 26 still awake. Maximum observed final linear speed was 0.615676 and angular speed 0.211752. This is an isolation failure, not actual-house acceptance. It shows that the source wall's mixed contacts/arrangement can remain unstable even without the detailed fixed remainder; it does not rule out additional remainder issues.

The test submission helper now retries a temporary Busy admission while asynchronous shape uploads complete, without scheduling the same simulation tick twice. The larger fixture exposed this harness limitation before it could begin the release.

The failing regression remains explicit; no sleep thresholds or forces were changed to make it pass. See `synthetic-wall-flat-floor.log`.

## Follow-up with reviewed contact fixes and wide floor

After the configured linear-slop bias correction and compound-normal temporal preference, the initial narrow-platform run still failed. The fixture was then widened to an 80×80 floor centered under the wall, so naturally toppling parts do not leave an artificial four-unit margin. This remains one valid bounded collider (below the 96-unit partition extent).

The wider case **also fails at 1,200 normal four-substep ticks**: 29 dynamic bodies, 27 awake, maximum linear speed 9.00602 and angular speed 1.473. The two fastest bodies remained inside floor bounds: body 29 at (15.9847,12.5867,2.48115), speed 7.77209; body 40 at (17.1141,12.227,2.5), speed 9.00602. Most other bodies remained near the source wall with smaller residual speeds. The snapshot cannot establish pose-derived speed, but it rules out leaving the synthetic floor as the explanation for these two positions.

Complete final per-body positions and velocity magnitudes are in `synthetic-wall-wide-flat-floor.log`. Actual-world acceptance was not run after this failed isolation. No forces, poses, sleep tolerances or gravity were altered by the fixture change.
