# r09 geometry and proxy correction — original work contract

The bounded correction and overview/detail checks are now recorded in
[r09-correction.md](r09-correction.md). Targeted close contacts and independent
acceptance remain open. The text below preserves the starting contract.

2026-09-08. Root owns this isolated ASSET-05 follow-up. The previous turn made
progress: it fixed the real guide-capacity failure and produced controls,
contact and motion evidence. No process from that turn remains live. No gate
has passed beyond G00; the full G14 goal remains active.

The authoritative installed candidate at the start is r08. Its exact records
remain in `summary.json` and `inspection-summary.json`. Preserve those files,
all older candidates, and all failed runs. r09 must pass authoring/cooking,
native/WASM assembly and actual renderer inspection before selection counts as
validated. ASSET-04 review and ASSET-05 parent acceptance remain open under D18.

Changes being implemented:

- Winch flanges move from x=±.30 m to ±.235 m, with .05 m thickness. The old
  cheek's inner plane is x=±.27 m, leaving .01 m clear space at both flanges.
  The drum shortens from .56 m to .42 m; seven cable wraps span −.18… .18 m.
  Actual post-modifier vertices must demonstrate ≥.009 m cheek clearance at
  every LOD, drum fit and no cable/flange interference before export.
- The helm's structural well must cut both its .16 m foot and the column
  above it: its declared .20 m depth previously ended inside that column.
- Helm, winch and cradle gain disjoint box proxies that preserve meaningful
  open spaces. Their build envelopes, socket frames, module limits and dry
  masses stay fixed. Their COM/full inertia are deliberately recomputed from
  uniform density over the new disjoint proxies; this is an explicit
  approximation, not a measured machinery interior. Solid-material buoyancy
  uses the existing thin-core policy and never seals the empty frame.

New metadata must demonstrate free sample points inside the open frame, solid
support points, disjoint proxy boxes, consistent collision/occupancy and no
buoyancy core outside its corresponding proxy. Both starters must retain
1,035 kg total mass, all seventeen exact socket connections and the expected
480 kg·m² yaw/roll inertia difference. The engine/propeller placement remains
the r08 corrected lower shaft. Inspect all three detail levels in both runtimes.

The propeller's conservative rotating-envelope box, generator's housing/cage
envelope and fine socket recess omission remain declared approximations;
they are not evidence of final contact fidelity or solid displacement. No
live motion, cable, latching, mission, material/cove or independent visual
acceptance is included in this correction.
