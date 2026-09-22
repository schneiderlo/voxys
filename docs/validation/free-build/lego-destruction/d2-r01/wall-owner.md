# Imported wall physics owner — D2

Date: 2026-09-18.

`ImportedWallPhysics` owns a separate `CannonPhysicsScene` for the selected imported section. Ordinary cannon scenery recooks cannot recreate fallen wall pieces at their source poses.

## Release and publication contract

1. Source admission uses `ImportedAssembly` and preserves every source ID. Initial geometry is available for CPU queries before GPU admission. Every initial component starts static, including components whose external support was not described by the bounded section recipe.
2. Manual release selects only parts belonging to an originally anchored component. It cuts the validated bonds inside that membership and its explicit external anchors. Parts outside that membership remain static. No impact force or random impulse is invented.
3. Old roots are replaced at the existing joined GPU mutation boundary. Because rendering follows physics encoding, `bindingsForEncodedTick` selects the new GPU handles once that mutation is encoded; CPU gameplay acceptance still waits for completion. Render matrices are part-to-root matrices; the renderer applies the GPU root transform exactly once.
4. While parts move, the caller must keep the actor safely at the cannon, refuse firing/building/saving and avoid publishing stale moving-body CPU AABBs. This owner returns no settled query geometry during that interval.
5. A sleeping observation only triggers a pause request. After the scheduling frontier joins, the owner requests a new snapshot in an owned zero-tick submission. It does not change or advance the borrowed world’s scheduling mode. It accepts poses only when all released handles are present/alive/asleep, the snapshot incarnation matches, and its tick equals the fully joined completed tick.
6. It replaces those sleeping roots with static roots at the certified positions/orientations. Only after that admission completes does it publish conservative per-cell world AABBs for walking/picking/camera queries. Rotated cell bounds may be conservative; this is not dynamic character/debris collision.
7. Rebuild is allowed while falling or after the 1200-completed-tick non-settlement failure. Existing accepted bodies remain owned until replacement. Pending admission must finish before another replacement. Clearing drains the scene before dropping handles; whole-world shutdown can abandon only after the owning physics world stops.

## Limits

- One manual experiment, gravity only. This does not implement cannon impact damage, waking frozen debris, or live actor/debris coupling.
- The static promotion is deliberate for this bounded D2 gate. D3/D4 must replace this restriction before allowing further impacts on debris.
- The owner consumes the free-build debug-snapshot stream. Other observers must not independently drain its packets.
- Graph/shape/body admission failures retain ownership and fail closed. A GPU failure is not repaired by claiming that the scene cleared.
- A source component without explicit validated support is retained static; proximity does not invent connections.

## Focused checks

`nix-shell --run 'bazel test //tests:imported_wall_physics --test_output=errors'`

**6/6 passed**, including five AMD Radeon 890M Vulkan GPU tests. Test wall time 42.8 seconds.

- Initial transformed query cells exist before GPU admission; invalid nonfinite placement cannot activate the owner.
- An explicitly anchored imported brick falls and settles under GPU gravity; an unanchored source component stays pinned. Certified static query cells match the new height. Rebuild restores source positions and clearing retires the owner.
- Rebuild during active falling safely discards obsolete observations; a subsequent release still settles correctly with all identities retained.
- A no-ground fall reaches the bounded non-settlement failure, publishes no fake resting geometry, and can be rebuilt successfully.
- Partial body-capacity admission failure cancels the partial reservation. Explicit Rebuild drains both accepted and failed replacement resources, then installs the original source once capacity returns. No pending-graph/permanent-failure trap remains.
- Render bindings switch to pending handles at their encoded mutation tick, before the CPU completion/publication update. The old handles are not used by the same frame that destroys them. Gameplay/query publication still requires completed proof.

Evidence: [wall-owner-tests.log](wall-owner-tests.log). This verifies the owner protocol, not the full-house browser integration or the normal commit hook.
