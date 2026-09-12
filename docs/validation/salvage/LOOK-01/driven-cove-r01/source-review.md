# Independent source review and corrections

Review performed during the 2026-09-12 session before runtime integration. These
are bounded source/CPU-structure findings, not additional game/GPU executions.
No screenshots, asset reexports or repeated cooks were used for the review.

## Art contract

The independent reviewer checked all 23 candidate-report file hashes, installed
recipe/dependency paths, six unique payload identities, exact canonical/LOD
contracts, GLB root/node declarations and cooked VMESH node names/pivots.
The author's actual geometry/material correspondence checks and clearance
calculations were inspected without rerunning the verifier. No unresolved
integration defect remained in those scopes.

One provenance defect was found: the recipe indirectly calls
`metric_materials.py` for linear color conversion but initially omitted it from
the input hash list. The author added it to the recipe's inputs and both
provenance records. This was a metadata-only correction; exported source and
cooked payload bytes did not change. Both provenance records retain:

- Original authoring recipe SHA256:
  `a9f0f2c55c99db2371ac97fbd44f051d4f2ec826d7298feab3b474a91912b1c5`.
- Corrected installed recipe SHA256:
  `4519f6c3e44dff450598fe78b6f06b1cc5ba173c28c536f746695b342420f35c`.

The original numeric verifier's rounded-normal exact keys failed at rounding
boundaries (`verify-r01.log`). The next 2e-5 normal-component tolerance exposed
the actual 1e-4 reexport difference (`verify-r02.log`). Final comparison requires
matching triangle correspondence, position error below 1 micrometre and normal
component error below 1.1e-4. Propeller positions/normals are exact. Winch maximum
position error is 7.45e-9 m; its maximum normal-component error is 1e-4 on LOD1
and zero on LOD2/3. Both `verify-r03` and final full-sweep `verify-r04` passed.
The asset was not regenerated to obtain those verifier passes.

All-angle propeller clearance uses the previously corrected r04 blade geometry;
the rejected earlier r03 propeller is not selected. That earlier defect and its
actual geometry correction remain in the preceding `../machinery-r01/` evidence.
This candidate preserves all original part geometry except numerical pivot
rebasing; only the existing winch flange material segmentation intentionally
changes appearance.

## Tick and rope identity

The first phase helper rebased a different rope handle before checking sample
time. A retired handle at tick 100 could replace the baseline of its successor
at tick 101; a later stale sample could then spin the drum or discard a real
new length delta. Root fixed this with a persistent observed-through watermark
checked before handle rebase and a retained freeze/detach tick floor. A different
handle at the same observed tick is ignored. Pre-pause samples cannot establish
a new baseline after resume.

The corrected helper was reread against that counterexample, pause/detach,
prepare/discard, finite/range and overflow paths. No further finding remained
under the Application contract: the caller supplies only independently validated
current attachment/body generations and observations no older than their command.
The five actual `CoveMechanismsTest` cases in `focused-r02-test.xml` subsequently
passed, including retired samples, pause/detach, discarded work and overflow.

## Renderer and journey evidence

The asset author independently matched renderer bindings to the actual cooked
nodes and source axes. The winch's exported Y pivot 0.1199999973 fits the declared
1e-6 tolerance. The renderer retains fixed-node transforms even when nodes share
a mesh and uses the chosen presentation LOD, rather than the canonical static
bundle. The first numeric pose run exposed an incorrect test color classifier:
ACES adds red/blue components to displayed linear green. The preserved
`handoffs/gpu-classifier-review.md` derives that result from the existing color
equations and records the existing hue-classifier replacement. It retains all
visibility/equality/motion thresholds and changes no renderer behavior. Root
reran only the affected numeric GPU case: `gpu-pose-r03-test.xml` records one
pass, zero skips, 0.118 seconds, with exact stationary-node equality, visible
moving-node change, static/live-root equality and blank stale generations.
Application and dock journey proof remains pending.

Driver review found that comparing wrapped angles alone can falsely pass a
frozen mechanism at a whole revolution and the wrong direction at a half
revolution. Both native/browser drivers now require nonzero-motion endpoints
with `abs(sin(expectedAngle)) > 0.1`, captured and completion-proven while input
is still held. Stable drive and exact accepted rope handles/ticks are required.
They also require positive live body identity, exact 48 material / 0 machinery
ownership, and scan uncaptured/native process errors. Their common docking
quaternion offset matches the real `[2.1,0,0.1]` boarding-to-helm contract.

Native persisted-design parsing was read against the actual save field order;
it hashes full part/connection bytes and recovery blueprints while deliberately
excluding authority incarnation/lease changes. Browser compares the existing
read-only blueprint export. Neither driver mutates private game state to pass.
The Application emits mechanism numeric fields at 17 significant digits so
near-tau phases and rope-length deltas retain sufficient precision.

Dock authoring was reviewed independently against actual cooked all-LOD flat
panel extents. Its preserved handoff records stricter insets and exact socket
exclusions. A sibling also reviewed the final generator, owner integration,
four cases and Application/build overlay without an actionable finding. The
original source arithmetic (98 vertices, 48 triangles, two draws, 7,760 bytes)
is retained with its original source-only label; the first actual fixture run
then measured the same byte count.

The first five-case dock run passed three CPU contracts and the existing full
scene owner case. Its sole failing case reported exactly two failed draw-count
expectations. Those used eight admitted plates rather than the four plates
visible to the test's fixed camera: the actual six draws include both inlay
materials. Root's correction compares against the actual culled baseline,
retains positive baseline/bounds and requires exactly two additional draws.
The single affected-case rerun passed in 0.475 seconds with zero skips and the
same numeric properties (`checks/dock-draw-r02-*`). The first run's opaque
color/depth and exact hidden-baseline restoration assertions passed, recording
78 changed opaque pixels, split 28 teal and 50 orange. No image was written.
Application journeys and user visual approval still remain pending. The first
native attempt later observed the predicted combined owner counter in its final
healthy state, but failed its driver navigation step before boarding.

The first successful combined WASM build emitted a new GSL dangling-lifetime
diagnostic on a reference obtained through a temporary non-owning span. Source
review confirmed that its admitted bundle's owned LOD vector remains alive
throughout rendering. A named span local expresses the existing backing lifetime;
no phase/geometry/draw behavior changes. Both native/WASM r02 builds passed,
and the new warning disappeared. The original review and both build rounds are
retained. Final WASM still reports 12 pre-existing Application translation-unit
warnings; this record does not claim a warning-free build. No CPU/GPU rerun was
performed for that expression-only clarification.

The first native actual-control attempt exposed an overly strict driver approach:
at feet `[4.68133,1.285,-52.3956]`, `interaction=board` was already available,
but the driver kept seeking an exact point and never pressed E. It failed after
30.167 seconds. Both drivers now follow the actual marked turn route through
`(6,-49.5)` and `(4.5,-49.5)`, ending the approach on the relevant real board/
helm/dock prompt. Physical action and confirmed transition remain mandatory.
No private state, camera, physics, collision or Application changed; no rebuild
or extra renderer checks were needed. Original failure evidence and the review
are retained.

The second attempt reached the corresponding dock-to-Workshop case: exact
waypoint navigation continued despite `workshop.canOpen=true`. Both drivers now
end that approach at the real availability condition, still requiring physical
B/click and a confirmed open Workshop. The second failed attempt retains passing
forward/reverse/stop phase pairs, but no completed journey is claimed. Its
process log additionally reports one surface-acquisition `status=1` error at
00:00:29.604. That original error is preserved; the navigation correction does
not fix it and the failed process is not described as clean.

The surface review traced native value 1 to the pinned API's named Timeout,
not a genuine acquisition failure. WASM has a different numeric value for the
same named status, so the correction uses the enum rather than hard-coding 1.
`Context` tracks a consecutive timeout: one warning, release any returned
texture, return before frame tickets, then an explicit recovery log on successful
reacquisition. All genuine-error branches and driver scanners remain intact.
Independent review also required the diagnostic flag to follow both Context
move operations and clear on shutdown. Those five assignments are now present
in `src/gpu/context.cpp`; all three context files are included in the batch.
Both platforms passed r03/r04 builds. The frozen native r02 executable used by
journey r03 recorded a timeout/recovery pair at 64.072/64.073 seconds, with zero
strict scanner error lines. It lacks the later lifecycle bookkeeping and is not
relabeled as the final package. The final native-r03 package is used by the
r04 journey, which later failed its drum-phase check. No complete journey pass
is claimed.

After r03 failed its repeated approach following a successful reverse refit,
root compared movement sampling with the existing delivery helper. The mechanism
helper released input but immediately allowed the next direction to be computed
from the same 10 Hz position sample. Both mechanism drivers now require a newer
player tick after release; every boarding intermediate step also stops on its
real board prompt. The native driver saves a bounded failed-approach trace on
failure. This was a source-supported candidate cause/correction, not proof of
the entire r03 failure. The subsequent actual r04 run completed navigation,
both free refits and all propeller cases, then found a separate runtime defect.

During real reel-in, the Application discarded the previous accepted rope
observation while waiting for a new motor command acknowledgement. An absent
observation tells the phase helper to clear its rope baseline, so the next
sample rebased instead of applying the first two reel ticks. Root now retains
the prior accepted observation when it is still the same current, alive rope;
pending motor acknowledgement is not treated as rope removal. Different handles,
actual detach, pause and stale-tick protection remain. The failing r04 evidence
records a −0.1333332061767578 m length delta but only half its expected drum
phase. Independent causality review and the r05 real integration run are pending.
Earlier helper/renderer unit passes did not cover this Application adaptation
mistake; this record explicitly retains the runtime failure.
