# Bounded compound contact patches

Current implementation: eight bounded normal patches for imported FreeBuild; one remains the default elsewhere. Direct GPU regressions pass. Full wall settlement is still a separate, incomplete gate. The sections below retain the measured progression and evidence.

## Measured problem

The original narrow phase stored one normal and four points per body pair. Real interlocking stud/underside geometry can simultaneously touch vertically and sideways. The 120-pose replay records a valid supporting manifold replaced by a tiny stud-side manifold. Choosing a preferred normal cannot represent both constraints. The proposed architecture must preserve both current geometric patches rather than force one to stand in for the other.

Normal persistence passed its reduced pair regression, but the complete released arrangement remained unstable. Do not treat that isolated pass as acceptance. Keep or remove that heuristic according to the ongoing controlled whole-arrangement comparison.

## Initial architecture proposal

Use multiple existing `GpuContactManifold` records per broad-phase body pair. Begin with a deliberately bounded two-patch mode for the measured support-plus-side case. A third incompatible patch must signal explicit overflow and fail the requested physical admission/step safely; silently discarding it recreates the present defect. General interlocking corners will require a measured three/four-patch budget or a broader design.

Each patch retains its own normal, four spread support points, friction center/basis, normal impulses, twist/rolling state and solver cache. Existing manifold ABI stays unchanged. Body broad-phase pairs remain unique. Contact records become unique by body pair plus patch slot.

A fixed two-slot mapping is the smallest initial pipeline change: raw contact slot = broad-pair ordinal * 2 + patch slot. Write valid sorted pair keys for both slots, with zero point count for unused slots. This preserves pair-key ordering for bounded previous-frame range lookup and avoids introducing a GPU prefix-sum allocator. Active-contact compaction already removes empty records; extend its scan/count to the raw slot count. Primitive pairs occupy their first slot; an optional one-slot mode can retain their existing memory profile.

### Required changes

- `shaders/physics_narrow_phase.wgsl`: collect compatible exterior patches into separate bounded normal clusters. Apply existing spatial point reduction independently inside each cluster. Emit all accepted clusters. Remove the need to choose one competing normal. Split broad pair count from raw manifold-slot count in count/finalize/active-compaction/commit paths. Preserve raw-slot identity in `pair.ordinal`.
- `src/physics/gpu/gpu_narrow_phase.hpp/.cpp`: explicit patch-slot configuration, checked multiplication/capacity, raw manifold capacity versus broad-pair capacity, scratch/dispatch sizing, new patch-overflow telemetry. Previous/current manifold buffers grow with configured patch capacity; the struct layout need not change.
- Previous-manifold matching: locate the contiguous previous body-pair range, then select the matching normal cluster using compatible normals, source features and local anchors. A patch slot alone is not durable identity. Bound every scan by the configured patch count; preserve deterministic ordering/ties.
- `shaders/physics_dynamic_solver.wgsl` and host setup: no new constraint equations are required. Each patch is an independent contact record sharing the same two bodies. Existing color/dependency scheduling must serialize conflicting records. Audit small-island fast-path degree assumptions: a two-patch isolated pair has two records and must not be processed concurrently as disjoint contacts.
- `shaders/physics_islands.wgsl`: repeated union edges for the same body pair are harmless, but capacities/counts must refer to contact records, not broad pairs. Keep fixed supports outside dynamic island unions.
- `shaders/physics_event_readback.wgsl`: emit the correct patch normal and unique slot; document multiple contact events per body pair. Future damage processing must aggregate appropriately instead of counting patch records as repeated cannon strikes.
- `src/physics/gpu/gpu_physics_backend.cpp`: budget raw/active manifold capacities explicitly, propagate fail-stop overflow, account for the larger persistent/scratch storage. Root scene admission must reserve enough contact capacity, not merely shape/body capacity.
- Validation/CPU mirror structs: retain the manifold ABI, but update telemetry and any tests equating broad pairs with manifold records. Audit all consumers of `pair.ordinal`, `broadTelemetry[3]`, `narrowTelemetry[22]`, and `solver_pair_count()`.

### Gates

1. Two patches generated for the reduced real stud/underside pair; floor and side constraints coexist in the same tick. Separate warm-start impulses survive ordering changes.
2. Deeper side impact remains effective while vertical support remains present. No invented contacts or forces. Patch-capacity overflow is explicit and rejects the completion frontier. The solver stops integration, but earlier force/CCD passes may have run: this is fail-stop, not transactional rollback.
3. Solver coloring, serial levels, island union and event paths accept two records sharing both bodies without races or doubled application.
4. Existing hollow catalog stacks remain stable at identity and the actual distant/yaw-π house frame; retain the solid baseline.
5. Released 39-part synthetic arrangement settles at unchanged gravity/damping/sleep settings, with deterministic complete body coverage.
6. Actual full-world release, collision query promotion, rebuild/save recovery, and rendered/browser review pass. Until then, D2 remains incomplete.

## Why not just add per-point normals?

The current solver uses one shared tangent basis, friction anchor, friction impulse, rolling/twist state and tangent mass per manifold. Merely storing a different normal on each point would leave those shared friction constraints inconsistent. Four total points also cannot reliably preserve a four-corner floor support polygon plus a wall contact. A correct per-point design would require more points, per-point/patch friction caches and rewrites of preparation, warm start, both solve paths, restitution, events and every shared WGSL/C++ struct layout. That is a larger change than reusing existing complete manifold records.

The point `impulses` vector is not unused storage: its Y lane carries pre-impact event state. Repacking normals into apparently spare lanes without auditing event consumers would corrupt impact reporting.


## Initial four-patch implementation (native direct gate)

- `normalPatchesPerPair` configures 1–4 fixed raw slots per broad pair; legacy worlds default to 1. Imported FreeBuild selects 4 and scopes logical pair capacity to 8192. The 384-byte manifold ABI stays unchanged.
- Authored compound exterior contacts collect compatible normals into independent patches. Each patch retains up to sixteen candidates before spatial reduction to four solver contacts. Separations are projected onto the final patch normal.
- Current patches match previous patches by normal with one-to-one cache consumption. Previous raw slot order is not identity. Existing anchor/feature matching remains inside each patch.
- Expanded raw history is scanned completely in multipatch mode. Per-pair patch overflow sets telemetry word 25; active solver capacity overflow sets word 26; either sets sticky word 27. Overflow cannot silently drop support. Root-owned solver and mandatory completion proof consume the sticky failure. The legacy single-patch capped scan remains unchanged and does not gain full-range active overflow detection.
- Invocation-private patch storage avoids a native shader compiler failure observed with large nested CandidatePatches return/argument values: the nested-value version validated but silently returned no authored exterior contacts. The otherwise equivalent private-storage path passes the direct legacy and multipatch tests. No generalized compiler claim is made beyond this observed local reproduction.

`multiple-normal-patches.log/xml` records three passing native tests: the original real-pose normal preference, simultaneous support and side contacts at the same poses, and a three-face corner. The corner verifies three simultaneous normals, distinctive warm-start impulses surviving rotated history slots, and a configured two-patch overflow remaining latched after the next separated tick.

This direct gate does **not** establish that the 39-part wall settles. The catalog stack, actual arrangement, full-world integration, full authored regressions, and browser acceptance remain separate required checks. Four patches may be insufficient for a stud with multiple simultaneous underside walls; that must fail explicitly rather than silently discard contacts.


### Eight-patch follow-up

The catalog gate rejected four slots explicitly: the 1×2 brick failed at tick 3, the plate at tick 8, and the 39-part arrangement at tick 3. A stud can meet underside support plus several distinct side normals. Production now allows a bounded 1–8 slots; imported FreeBuild uses 8 with 8192 logical pairs (48 MiB total double-buffered raw history, matching the old 65536×1 allocation). Overflow still fails rather than truncating.

`eight-normal-patches.log/xml` records the same three passing direct tests after expansion, including trihedral capacities 8/4/2 and actual support+side at capacity 8. Narrow initialization checks expanded buffer byte limits and full-scan dispatch limits before allocating. Telemetry word 28 records required patch high-water (a lower bound on overflow). Full arrangement settlement remains unproven at this point.

Independent review of root-owned failure handling found the required proof buffer copied after batch work, retained through ring ownership, and completion dependent on both the matching proof and submission fence. Sticky failure cannot disappear in a later clean narrow tick. The guard prevents solver integration, but earlier forces/CCD stages in already queued ticks can still execute; this is rejected completion, not rollback or a guarantee that every GPU byte freezes immediately.


### Unmatched friction-cache repair

Eight patches removed the observed overflow, but did not yet settle the complete arrangement (reviewer reported 28 awake at 1200 ticks, max linear speed .1496 and angular speed .1063). A separate cache audit confirmed per-point matching is one-to-one and tangential bases are reprojected correctly. It found manifold friction/twist/rolling impulses were retained on normal coherence even when no point anchors matched.

The relocation regression moves a cube .5 units across a floor, beyond the existing recycling radius. Before the fix, all normal impulses reset correctly but seeded tangent impulses 3/4, twist 5, and rolling 6/7/8 survived at the new lever arm. The implementation now requires at least one matched current anchor before carrying manifold friction/torque forward. `unmatched-friction-before.log` captures failure; `unmatched-friction-after.log/xml` captures both regression and trihedral test passing. The latter also now verifies dense solver results commit to all three expanded raw ordinals, not merely slot zero.

This is a measured cache correctness fix. The subsequent complete arrangement result is not implied by these direct tests.
