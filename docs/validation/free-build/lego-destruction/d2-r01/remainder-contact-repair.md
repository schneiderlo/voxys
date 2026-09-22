# Selected-wall collision repair

Date: 2026-09-18.

The first actual-house release did not settle: 28 of 29 dynamic pieces stayed awake, with large unintended motion. The wall owner correctly refused to publish them as static.

## Cause established from source and collision data

The exporter correctly removed all 39 selected parts before joining the remainder. Independent Blender checks found 2,140 source instances, 39 selected objects, and 2,101 remaining objects. The remainder contains 846,137 triangles, almost exactly 82% of the 1,031,875 **unselected** source triangles. It is not a duplicated rendering mesh.

The collision bake was designed for character walking. It sampled at 0.4 × 0.2 × 0.4 studs and applied morphological closing to fill seams too small for a 2.24-stud character. That operation recreated substantial collision inside the removed wall. All 29 release-eligible bricks had true shell-volume intersections, not merely overlapping hollow envelopes. One 1×1 brick overlapped a static remainder box through approximately 0.7 × 1.2 × 0.96 studs of its envelope.

## Bounded correction

`bake_ldraw_collision.py --wall-section <wall.json>` now:

- Preserves the coarse occupied union outside the selected wall's local region.
- Resamples actual remainder mesh surfaces inside that region at 0.2 studs, without morphological closing.
- Removes only source-identified selected shell/stud proxy volumes from local surface voxels. These are the same five shell boxes and four-strip stud proxies used by the imported-part catalog. It does not subtract one large facade envelope.
- Reserves 0.0201–0.0401 studs of local clearance at those interfaces to survive the runtime's outward 0.02-lattice collision quantization. This is an explicit approximation; fixed neighboring surfaces remain around the released members.
- Merges only adjacent fragments with matching other-axis extents, preserving the occupied union.
- Refuses more than 6,000 remainder boxes in this refinement mode. The original bake's 5,000-box guard remains unchanged. Runtime CPU/GPU ceilings are unchanged.

Final count: **5,973 boxes** (4,118 outside/clipped coarse boxes plus 1,855 local refined boxes).

## Checks

- All **39** selected parts, totaling **479** shell/stud proxy boxes, have **zero** remaining overlap at a 1e-7 intersection threshold.
- Static occupied volume outside the local region is unchanged: **6,361.424 cubic studs** before and after, delta **0**.
- Rebuilding the final recipe produces identical header geometry.
- The final collision JSON records exact source-blend, wall-section and recipe hashes.

Evidence: [initial overlaps](initial-remainder-overlaps.json), [refined audit](refined-remainder-audit.json), [source membership](remainder-source-membership.json).

These are geometry checks. The actual-house GPU run must separately prove resource admission, stable contacts, settlement and the resulting traversable opening.
