# Body/rope observation and player resume preparation

2026-09-09. Scoped progress toward SAVE-04, under D20/D21. The playable cove
now displays its actual solver cable length. This is not a complete expedition
save, a certified full-world checkpoint, or a passed gate. No screenshots,
gate commit, or durability acknowledgment were made.

## Implemented

`DebugSnapshotRequest` can include an exact attachment-slot range alongside its
existing body range. `DebugReadbackRing::encodeCopies` checks every source,
alignment, size and free slot before encoding any copy. It concatenates body
and attachment bytes into one staging slot and one map. Tick, selected ranges
and submission serial belong to that packet; a later ordinary copy clears the
attachment metadata. The existing single-copy API delegates to this path.

The GPU backend copies requested live constraint records after the same
completed solve as the body packet. It returns target length, motor speed,
anchors, limits, endpoint generations, allocated generation, alive/broken
flags and break evidence. It does not reconstruct cable length from distance
between endpoints or integrate CPU frame time. This matters for slack ropes,
force-limited stalls, payout, and breaks. Broken and destroyed slots remain
distinguishable; slot reuse returns the new generation.

Owned packets retain the existing submission/error-scope completion and GPU
tick proof requirements. Legacy unowned packets retain incarnation zero and
must not be treated as certified. Pose/constraint delivery does not imply that
the independent event stream or GameSession has reached the same boundary.
No transient body/attachment/shape handle is a canonical save identity.

The optional attachment readback budget defaults to 16 records, 128 bytes each,
per existing staging slot. With the default three slots this reserves another
6,144 bytes. Combined allocation is checked against device buffer limits.
The cove requests its one rope only: 128 extra bytes per requested observation,
not one whole attachment pool per simulation tick. Zero attachment capacity
disables this optional range. Invalid/oversized attachment requests are ignored
as a whole by the existing void request API; callers must preflight ranges and
check returned identities/counts. Existing body-range clamping is unchanged.

The application observes the rope in the same packet as the boat and cargo,
checks matching live handles/endpoints, and waits for observation through the
control's tick before showing that control as confirmed. Read-only JSON adds
`ropeObservedTick` as a lossless decimal string and nullable `ropeLength`.
The browser tow panel shows actual cable length separately from endpoint
distance. Hook/release, reeling, payout, sailing and LEGO terrain are preserved.

`CovePlayer::State` now describes local feet, vertical speed, mode, aboard flag
and diagnostic counters without handles or inputs. Feet are in authored boat
coordinates while aboard, cove coordinates otherwise. To restore, initialize
the accepted scene, apply the restored boat transform, then call `restore`.
It validates finite/bounded coordinates, reachable vertical speeds, legal mode
combinations, helm location, clear standing room and required support. A bounded
candidate copy keeps the current player and pending controls intact on refusal
or an exception from terrain evaluation. Successful semantic resume preserves
pose/vertical velocity and clears fractional catch-up, queued jump and queued
interaction. Counters saturate rather than wrap.

Current player profile bounds are absolute local coordinates <= 1,000,000 m,
vertical speed in [-30, 6] m/s, zero vertical speed for non-airborne modes,
swimming at local water height -0.8 m, and helm feet within 1e-6 m of its
authored standing point plus 0.005 m skin. The scene collision/support check is
additional to these bounds. This C++ state is not a byte format and is not yet
called by an in-game load path. Animated-water character coupling is unfinished.

## Verification

`results.json`, source hashes, package hashes and original logs are retained
beside this document. Final outcomes:

- 51 focused native cases pass on AMD Radeon 890M / RADV Vulkan, with no skips.
  They cover combined-copy preflight/capacity/reuse, actual reeling/payout and
  unchanged targets during readback-only frames, released/reused generations,
  broken-rope/event agreement, owned completion, event backpressure, existing
  physical cove/LEGO terrain, and player restore at helm, walking aboard,
  mid-jump and swimming. Invalid restore leaves queued interaction intact;
  successful restore clears unexecuted controls and fractional time.
- Native and shipping WASM application builds pass. Existing WASM warnings
  remain in the build log; this was not a warning-free build claim.
- 14 browser UI lifecycle cases pass.
- One real Chrome 152.0.7977.82 hardware journey passes 13 recorded gameplay
  stages plus drained Leave/navigation. It uses actual CDP key/mouse events
  and read-only JSON, with screenshots disabled. Cable target changes from
  6.34337 m when hooked to 4.34337 m after reeling, then 5.3767 m after payout.
  Towing, release, steering, walking on the moving deck, Reset and resize pass.
  Terrain remains LEGO; balances remain 48 material / 0 machinery. The terrain
  variant permits a real overload during sailing, but this run stayed intact.
  This run did not exercise banking or persistence.

All attempts passed; earlier build/test logs preserve narrower intermediate
scope. Player restoration itself has native domain tests and WASM compilation,
not a browser load journey. The unchanged 143-case logical-save WASM suite was
not rerun for this physical observation change. No full repository/gate check
was claimed.

## Continue directly toward actual save/load

1. Add an authoritative pause/drain owner for a save request. Stop new input,
   topology and scheduling, drain submitted ticks, and join body/rope snapshot,
   complete ordered events and accepted GameSession state at one tick. Do not
   call the CPU `advanceOneTick` path to backdate GPU topology. The present
   player's local clock and render-time water field still require an explicit
   capture boundary; these records alone do not certify a whole world.
2. Implement the bounded versioned physical archive plus full logical SVSC
   checkpoint. Map roots/rope endpoints to durable build, part and cargo IDs;
   include topology/content identity, root motion, water state/time, player,
   cargo securing/banking state and rope target. Modules/settings already live
   in the logical build. Recompile derived collision/mass and create fresh GPU
   handles on load. Do not save raw structs, matrices or resident buffers.
3. Couple that archive to the existing Linux and browser generation stores.
   Establish independent world-slot identity and serialized save ownership;
   missing known saves must not silently create fresh resources. Browser
   archive validation must call the actual bounded host decoder, not the test
   storage fixture. Use an owned byte-buffer bridge, not a multi-megabyte ccall
   string on the one-MiB WASM stack.
4. Restore at a controlled physics boundary with the same meaningful motion,
   aboard/helm state and rope target. Neutralize transient helm/reel inputs;
   retain physical velocities. Publish SessionRecovery's retired-parent and
   fresh-child lineage atomically before admitting durable new work. Implement
   a real typed storage acknowledgment before releasing durable journal data.
5. Add actual save/load/world selection controls, then exercise save/load while
   towing and after release/banking. Latching is still unimplemented and cannot
   be checked off. Finish Windows storage, world export/import, migration,
   durable journal/receipt compaction and remaining SAVE-01–04 requirements.

## Reproduce after relevant changes

```sh
nix-shell --run 'bazel build -c opt //tests:voxy_tests //:voxy_native'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="GpuPhysicsTest.*Attachment*:GpuPhysicsTest.BodyAndWinch*:GpuPhysicsTest.CombinedReadback*:GpuAuthoredShapes.Live*:GpuEventReadback.*:CoveMovement.*:CoveNavigation.*"'
node scripts/test_salvage_preview.mjs
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8'
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_TERRAIN=1 VOXY_SMOKE_REPORT=/tmp/capture-browser.json VOXY_SMOKE_COVE_PLAYER=/tmp/capture-journey node scripts/smoke_integrated_wasm.mjs /path/to/packaged-web salvage-cove
```

The tested package is `/tmp/voxys-physical-capture-web-r01`; it contains `web/`
and the three built `voxy_wasm.{js,wasm,data}` files. The browser fixture's
temporary server and profile close when the check completes. Its URL is not a
persistent user preview. No existing preview server was replaced.
