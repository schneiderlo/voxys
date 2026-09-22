# Independent rear-model critique — 2026-09-16

Requested by the owner after the rear-model implementation. Reviewer:
`rear_figurine_critique`, comparing `rear.png` against the owner's rear reference,
with `front.png` as a preservation check. No implementation edits by the reviewer.

**Verdict: r05 does not yet match the reference.** Basic toy proportions are
close; the rear details remain substantially different. Do not close the
reference-fidelity or owner-acceptance gates.

Priority corrections:

1. Hair: replace the smooth dome and long vertical strips with staggered short,
   broad upper/middle/lower locks. Middle locks should sweep laterally and end
   visibly over lower tiers. Inspect the final envelope clamp for flattened
   relief. Preserve the better front silhouette.
2. Sockets: the four openings read as dark domed buttons. Establish visible blue
   rims, sidewalls and flat recessed floors using controlled normals; inspect
   a neutral clay render from an oblique angle. The upper sockets need the
   internal vertical divisions visible in the reference.
3. Pelvis: the narrow hanging central sliver and bright surrounding gaps need
   a wider, rounded blue connector. Keep the lower-leg gap clean.
4. Sleeves/wrists: the elbows sit too far out, forearms look thin and vertical,
   and too much yellow wrist is exposed. Bring the sleeves closer to the torso
   and extend the cuffs while preserving hand attachment alignment. The prior
   arm direction was liked by the owner; avoid a wholesale redesign.
5. Legs/heels: introduce the reference's slight taper and heel flare instead
   of constant-width rectangular rear columns. Keep sockets round.

Lighting and camera differences exaggerate gloss/color discrepancies. Match
the bright neutral reference setup before changing exact material values.
Lighting does not account for the missing layers, pelvis shape or long wrists.

## Parent's independent mesh check

Ray queries against the installed Blender leg meshes confirm four genuine
blind cavities. Each center ray reaches a floor at rear-axis coordinate
`-0.06625`, behind the main rear face at `0.13125`: depth `0.19750` author units.
Geometric floor normals point toward the opening. This confirms Boolean depth,
not visual readability or the correctness of interpolated shading normals.
The review's button-like appearance finding therefore remains valid.
