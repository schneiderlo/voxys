# ACT camera lane independent reviews

These are bounded source/metadata reviews, not a runtime or gate approval. No GPU, image or game journey was run by this reviewer.

## Player collision

Read `src/game/expedition/cove_player.cpp` sphere/OBB/capsule helpers, packet setters and the corresponding additions in `tests/test_cove_player.cpp`.

Three concrete defects were reported to the owner and corrected:

1. A tangent terrain hit at distance zero could mask an equal-distance OBB overlap. Overlap now wins a tie and a focused case covers the combined result.
2. A first nonzero root pose packet could certify camera clearance before any cargo/empty scene packet was bound. The query now requires that explicit matching packet.
3. Raw-distance conservative advancement exhausted 32 iterations on a nearly parallel clear wall path. The step now uses the convex separating-plane closing speed. A 12 m near-parallel miss and a real shallow hit exercise the difference.

The final deltas and cases were read and found consistent. Player owner runs their focused cases; none were duplicated here. The old nine-point ground-support approximation was separately reported; an exact capsule GroundSupport callback and diagonal-stud regression were added by the owner.

## Rigid animation runtime

Read `src/game/assets/rigid_animation.{hpp,cpp}`, relevant `rigid_prefab.cpp` validation, and `tests/test_rigid_animation.cpp`. No actionable byte-range, hierarchy, immutable ownership, basis, anchor or pose-transaction finding. The owner’s CPU check then exposed exact 180-degree sign ambiguity. The later canonical quaternion delta was reviewed: dominant-component sign normalization maps q/-q identically before the ordinary shortest-arc dot correction, giving a deterministic equal-length arc. Runtime test results remain the owner’s evidence.

## Robot fixture integration

Read root’s `src/render/salvage_asset_fixture.{hpp,cpp}` robot delta. Re-admission precedes reservation; appended upload index follows helper and optional dock correctly; existing 256-node/512-draw capacities apply; the robot shares the owned path/fences/shadow pass. No render ownership/indexing defect found. One telemetry gap was reported: publication resets generic draw counters but initially left robot/dock counters from the prior owner. Root owns any source correction.

Numeric owner/pixel/retirement and full 64-brick-plus-robot tests were subsequently authored by this reviewer. Both then passed in the coordinated GPU run: see HANDOFF.md and checks/robot-fixture-gpu-r01-summary.json for actual owner bytes, pixels and retirement evidence.

## Robot core envelope finding

Read final-r03 recipe/checker reports and decoded actual installed LOD0 source vertices without rendering. The checker tested horizontal cylindrical radius only. Actual `robot_head` vertex `(0.215266317,1.678266168,-0.180266336)` is 0.395306968 m from the physical capsule upper endpoint `(0,1.4,0)`, thus 95.307 mm outside its 0.3 m radius. Torso is contained (maximum gap -34.5 mm). This core-head defect is distinct from the parent-accepted animated hand/foot reach. Reported to root and author for correction; final corrected art review is pending.


## Final completed-presentation and rounded-head delta review

Read Application `CharacterPresentation`, `renderSalvageAsset`, `updateCoveCharacterView`, the observed root/cargo packet publication and input/update ordering, plus camera actions 320–325. No remaining actionable issue was found in this bounded cache review: the camera checks every admitted root and cargo tick against the player collision packet; body placements and robot use that same CPU packet; rejected/incomplete camera results restore the cached Camera and replay the cached placement/robot/harbor/cable semantics. No GPU body handle or borrowed socket span is retained in the cache. Before the first accepted presentation there is no fabricated actor frame. Workshop invalidates the cache. This does not claim an actual-host frame or broad audit of all session transitions.

The earlier core-head clearance finding is resolved by robot package-r04. Read `author_cove_robot.py`, `check_cove_robot.py`, installed-data test delta and `geometry-r04.json`: the exact point-to-segment capsule check includes both hemispheres and every indexed neutral head/torso/pelvis vertex. Recorded largest head distance is .299740269 m and torso .277353567 m within the unchanged .30 m capsule. The rounded shell replaces the offending box corners. Intentional animated extremity reach remains the previously agreed presentation limitation; no full animated capsule-containment claim is made. The sibling's final rigid-animation test run passed 6/6; this review did not run another cooker, image or GPU process.


## Independent browser camera ownership review

`/root/builder_usability` reviewed the final browser Camera section and controller delta read-only and reported no actionable finding. Scope: section-only pad focus; Menu/Back/Escape/F2 closure; workshop/session/visibility/blur revocation; fresh click permission checks; engine-authorized paused options; held-key release safety for a mouse-opened drawer; and identity-checked global/listener cleanup. The reviewer ran no additional Node test, app, layout measurement or screenshot. This is independent source review, not actual browser runtime evidence.
