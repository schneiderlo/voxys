# Compound-patch settlement checkpoint

The contact-patch change is not yet accepted for the released wall. All four small catalog stacks settle with eight slots, but the full selected arrangement does not settle within its existing 1200-tick gate.

## Test conditions

`//tests:imported_stack`, filter `Catalog/*:ImportedWallFlatFloorGpu.*`. Existing gravity, damping, solver substeps and sleep settings are unchanged. Both fixture setup sites request eight authored contact patches. The synthetic wall uses all 39 real source parts, releases the 29 anchor-connected parts, keeps the other 10 fixed, and uses an 80 by 80 floor at the lowest eligible occupied-cell height. It intentionally does not contain the static house remainder or real terrain.

## Results

| Case | Four slots | Eight slots |
| --- | --- | --- |
| Three 3005 bricks | Pass | Pass, 34 ticks |
| Three 3004 bricks | Contact-capacity failure, tick 3 | Pass, 34 ticks |
| Three 3023 plates | Contact-capacity failure, tick 8 | Pass, 34 ticks |
| Three distant/yaw-pi 3005 bricks | Pass | Pass, 42 ticks |
| Full selected 39-part arrangement | Contact-capacity failure, tick 3 | Fails settlement after 1200 ticks |

The eight-slot wall ends with 29 dynamic parts, 28 awake, maximum linear speed 0.149629 and maximum angular speed 0.106302. No capacity error is reported. This isolates remaining instability to the mixed arrangement/contact solution; it is not evidence that the real-house gameplay gate passes.

Four-slot failures triggered the mandatory owned proof and stopped completion certification. The test helper now exits immediately on failed proof and reports the encoded/completed frontier instead of waiting ten seconds with an empty error.

Evidence: `multipatch-four-slot-stack.log/.xml` and `multipatch-eight-slot-stack.log/.xml`. Keep the full-wall assertion unchanged. Do not promote moving poses to static or change global sleeping thresholds to turn this checkpoint green.
