# Independent visual review — 2026-09-16

Requested explicitly by the owner after the first matching pass still felt wrong.
Reviewer: separate `figurine_visual_comparison` agent. This review compared the
owner's close-up reference against `model.png` in this directory (the first
matching pass, source revision `/tmp/voxys-sculpted-figure-r08`). It was not a
review of the later corrected asset.

The candidate does **not** yet match the reference. Ranked corrections:

1. Taller visible face, not a uniformly wider head. Estimated face/torso height
   was 0.62 in the target versus 0.46 in the candidate; head/torso width was
   already close, approximately 0.80 versus 0.78. Camera differences limit these
   projected measurements; do not treat them as measured 3D dimensions.
2. One dense molded hair shell with broad flattened waves, shallow grooves and
   organized side layers. Candidate strands resemble ropes and reveal a cap.
3. Sloping upper sleeves, a definite elbow bend, straighter lower sleeves and
   flat cuffs. Reduce swollen shoulder caps.
4. Longer exposed yellow wrists and narrow, rounded C openings. Candidate hands
   have angular wedge-shaped ends. Outer hand size is reasonably close.
5. A narrow curved blue hip connection with tight seams, not an open dark cleft
   or a large projecting center block. Keep generally similar leg/foot size.
   Remove the thin double sole line and round molded edges.
6. Joined curved pocket prints, a yellow neckline crescent, nested shirt collars,
   smooth lapels and stronger eyebrows.

The proof camera was more oblique than the target (estimated eye-line slope
9 degrees versus 2). Use a near-frontal comparison before making more proportion
judgments. Beige/salmon studio colors also differ from saturated toy plastic;
separate proof lighting/tone mapping from game material changes.

Owner acceptance remains open. Automated mesh/runtime checks prove technical
validity and behavior only; they do not establish reference fidelity.
