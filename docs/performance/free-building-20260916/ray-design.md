# Third candidate — identical creative aim-ray reuse

Prepared after Change 2's separate CPU profile, before implementation. Remaining
inclusive CPU: updateTarget63.71%, construction validation39.50%, terrain sphere
sweep26.67%, raycast25.60%, partFor9.35%, JSON9.67%. Nested values overlap.

Updated ranking (impact × confidence / effort):
- Reuse identical creative aim rays: 4×4/2 =8. Investigate first.
- Certify sorted part ranges once per validation, then lower_bound with first-match
  linear fallback for unsorted/oversized standalone inputs: 3×5/2 =7.5. Defer.
- Reuse validation scratch: 2×3/4 =1.5. Defer lifetime/failure-path complexity.
- I/O changes: 1×4/4 =1; still zero file/network calls in steady replay.

Proof: AdventureSpatialQueries::raycast is const/noexcept and has no side effects.
It reads only its terrain, immutable published solids/sector map, origin, direction
and maximum distance (fixed25m here). In the shipped creative Application the
installed terrain is loaded before runtime initialization; creative input bypasses
terrain-edit controls. Accepted solid publications have revision identity. Key
world+epoch+geometry revision plus all six IEEE754 bit patterns of origin/direction;
bit keys preserve signed-zero distinctions and never rely on approximate equality.
The key is built after the original camera/input calculations. Reuse only in
creative mode under FE_TONEAREST. On misses/nondefault rounding/legacy adventure,
execute the original query unchanged. Store one complete RayHit (including IDs,
normal, exact distance, completeness and terrain flags). Continue every subsequent
observation, target, support and preview step as before. Initialization and successful
restore clear the entry, because different checkpoint contents can reuse revisions.
Share that default-rounding eligibility with the earlier preview-result cache,
so a standalone caller changing rounding also recomputes placement validation.
Actual edits still independently validate. No query arithmetic, tie ordering,
terrain traversal, camera sweep, saved state or input processing is changed.

Test: aim at an existing brick, warm the result, restore identical-shaped geometry
with new durable IDs but the same world/revision and camera ray, then remove the
new brick through the public command. A missing restore invalidation must leave a
stale target and fail that regression. Existing exact replay oracle additionally
covers changing aim, walking, placing/removing and geometry publications.

Measurement gate: unchanged24-run matrix versus original and Change2, exact output
and save equality, existing latency/throughput/RSS guards. Keep only a useful measured
improvement. This proposal does not claim a measured benefit yet.
