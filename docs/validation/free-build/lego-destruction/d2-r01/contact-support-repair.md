# Contact support repair — native GPU evidence

Two concrete contact defects were corrected without changing gravity, damping or sleep thresholds:

- Authored face IDs identify patches, not individual corners. Warm-start matching now chooses the closest old local anchors and applies the existing recycle-distance bound to authored matches.
- Compound contact collection previously dropped every patch after the sixteen-candidate scratch buffer filled. A ten-foot test spanning X ±9.5 retained no contact beyond X -2.5. Inter-patch streaming reduction now preserves deepest/spatially spread representatives and continues collecting later patches. Generic primitive clipping remains unchanged.

The direct coverage regression passes with both primitive and authored ground. The warm-start regression verifies reordered corner impulses and rejects stale distant anchors.

## Isolated stacks

Three real 3005 bricks, three 3004 bricks, and three 3023 plates each settle through the wall owner's normal certification in 34 ticks. A separate raw hollow/solid comparison settles both by tick 31. The hollow stack formerly exceeded the normal sleep threshold on 259 of 300 ticks; after repair it has zero such ticks, peak linear speed 0.02288 and angular speed 0.00659. Those five cases passed together. A further actual-house-origin/yaw-π 3005 stack passes in 34 ticks; its log/XML are preserved here. The first five-case run logs were overwritten by the later focused run, so those values are recorded from the observed test output, not represented as a retained raw log.

## Authored suite

The full suite passed its first 55 cases without an assertion failure, then reached its existing 300-second process timeout during case 56. Only the remaining two cases were rerun; both passed. Their log/XML are retained. All 57 cases have therefore passed across the split run; the full timed-out raw log was overwritten by the focused rerun. No uninterrupted full-suite pass is claimed.

## Actual house remains a failing gate

The full-world native runtime retains 160 nearby props and admits 71 bodies: 8,694 cells, 43,191 faces, 17,317 nodes and 3,067,344 charged shape bytes (including the intact wall). Public cannon controls, pause restrictions, session-only save refusal, and rebuild/save recovery execute correctly.

However, all 29 released pieces remain awake after 1,200 ticks. The opt-in 120-tick body trace proves actual pose jitter, not merely biased stored velocity. The base boundary-supported pieces are comparatively stable; the upper masonry/plates exhibit continuing small oscillations. There is no accepted full-house settlement or browser/render gate. The trace is synthetic native input, not browser evidence.

The saved runtime-contact-fix-trace.xml contains the raw body/source-label trace, and wall-motion-late-summary.json reports ticks 61–120 separately from initial settling.

## Subsequent bounded diagnostics

The reviewer fixed another independently reproduced defect: tiny rotated bottom-edge contacts selected a side-face ID rather than the face most aligned with the contact normal. The actual house still did not settle at the default four substeps after that correction. A test-only eight-substep comparison also failed (29 awake at 1,200 ticks; endpoint max linear speed 0.500 versus 0.258 at four substeps). The temporary test setting was removed. These logs are retained as runtime-feature-substep-comparison and runtime-eight-substep-comparison.

A direct narrow-phase replay reused the exact two cut source shapes and the recorded 120 COM/principal poses for source IDs 74942832682433725 (98283 below) and 139456561832398802 (3005 above). All 120 samples have contact. 118 choose downward-Y support; ticks 123 and 140 instead choose nearly horizontal -Z normals (Y=-0.000797 and -0.001709). Side penetration is only about 0.0018, with other retained side points separated by +0.0084. Thus incompatible stud-side patches can replace the whole support manifold. The replay uses runtime-default slop/speculation/recycle settings and no solver step; its passing status means capture completed, not that contact behavior is accepted. Raw input poses and output log/XML are preserved beside this report.

## Normal persistence correction

A self-contained two-catalog-brick regression reduces the recorded failure to two poses. Before correction, the tiny stud-side overlap changes normal Y from near -1 to -0.000797. The corrected inter-patch choice prefers the prior manifold's normal cluster only when competing depths differ by no more than the existing 0.005 linear slop. It retains only currently generated contacts and uses no world-up heuristic. The regression also moves the upper brick 0.03 farther into the side: that genuinely deeper collision overrides the preference and chooses its side normal. Both checks now pass; before/after logs are retained. Full wall validation remains pending this correction plus the separately reviewed solver-slop fix.
