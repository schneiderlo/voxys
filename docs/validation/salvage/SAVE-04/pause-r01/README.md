# Expedition pause and completed observation boundary

2026-09-09. Scoped SAVE-04/SIM-04 progress. **P** or the browser **Pause** button
now pauses the playable LEGO cove. **P / Resume** continues it. Leave remains
available while paused. This is an actual scheduling/input boundary, not a
screen overlay. No full save archive or save/load UI is implemented by this
checkpoint. No screenshots, gate completion or gate commit.

## Scheduling contract

`PhysicsWorld::setSchedulingPaused(paused, finalTicks=0)` is supported only by
an active owned GPU execution frontier. CPU/legacy backends refuse explicitly.
It preserves existing scheduled ticks, clears accumulator debt and stops both
automatic accrual and ordinary explicit tick scheduling. Optional `finalTicks`
are reserved atomically when entering pause, within the existing pending and
in-flight bounds. Repeated final-tick reservation, reservation while resuming,
an active owned submission/prepared mutation, capacity overflow or a failed
world refuse without a partial scheduling change. The accumulator/fixed-tick
mode itself is preserved. Resume clears clock debt again. Shutdown clears pause.

This API is a scheduler boundary, not a global mutation lock: the composition
owner must close its input/transaction paths and drain queued commands and
observations. It does not neutralize controls on its own or attest full-world
checkpoint validity. Rendering uses interpolation alpha one during scheduler
pause, displaying the actual final pose instead of the previous interpolation
sample. GPU submissions and lifecycle polling remain available while paused.

## Cove owner sequence

The transient application owner has Running → Requested → Draining → Paused
states. These are not a save schema.

1. A pause request is admitted only for the active boat, outside the workshop,
   with no pending session transaction, launch, securing operation, control
   request or unapplied tow action. P uses action 90; Resume uses 91. The app
   blocks all ordinary cove actions during Requested/Draining/Paused. Leave
   retains its reserved control path. UI buttons follow the same state.
2. Stop player input and new time scheduling immediately, discard unexecuted
   player jump/interaction and fractional catch-up, and release/reset input.
   Already scheduled GPU work still executes. The player retains local feet,
   mode and vertical velocity; an aboard player's world pose follows the final
   observed boat transform while that work drains.
3. Once scheduled equals completed, neutralize helm and the live rope motor.
   Queue its normal future-tick motor command and reserve exactly one final
   tick with `setSchedulingPaused(true,1)`. This avoids leaving a neutralization
   command stranded beyond the saved tick. Body velocities are not zeroed.
4. Wait for that exact target tick in the complete owned frontier, boat and
   cargo observations, rope observation when allocated, and ordered event
   coverage. Confirm the GameSession tick at the same value with no pending
   execution. A live rope's observed motor must be zero. Only then report
   Paused. Each body/rope packet retains its existing matching shape/handle/
   incarnation checks. No topology mutation is backdated into a finished tick.
5. While paused, stop requesting repeated body observations. Rendering and
   resource ownership still run. The cove water clock remains frozen. Resume
   clears input again and resumes scheduling without spending time accumulated
   during the pause. Leave first re-enables scheduling so its future destruction
   commands can execute and their normal body/shape/event retirement can drain.

Requested/Draining has a ten-second active-frame bound; each frame contributes
at most 0.25 seconds, so a suspended tab does not immediately exhaust it. Missing
complete evidence fails the cove rather than claiming a successful pause. The
existing owned GPU no-progress/error guards still apply. Deliberate missing-
packet, timeout, hidden-tab and device-loss fault tests remain unfinished.

Read-only JSON now contains `pause.phase`, `pause.canPause`, decimal-string
`pause.tick`, and numeric `pause.waterTime`. The ready flag still describes
scene readiness; pause is a separate explicit state. Pausing is refused during
workshop edits/preparation rather than silently discarding a draft.

The cove has its own elapsed water clock, advancing only during running play
(each update's contribution is capped at 0.25 seconds). Water GPU evolution,
camera sampling and initial/recovery placement use that same clock. Other
scenes retain their prior global clock. This freezes water across pause and
avoids a wall-time jump on Resume. **SIM-05's per-fixed-tick WaterField is still
unfinished:** ordinary catch-up physics can still sample a render-produced
surface. The final neutral tick uses the frozen cove time, but this does not
certify deterministic water replay or general character/water dynamics.

## Verified evidence

Original logs, browser results, source hashes and package hashes are stored
beside this report. The final native build and shipping WASM build pass; the
WASM log retains existing warnings. Fifty-two focused native GPU/cove cases
pass with no skips on Radeon 890M / RADV Vulkan. The new live GPU case proves:

- atomic rejection of an oversized final reservation without losing queued work;
- draining pre-existing ticks while wall time cannot add more;
- no ordinary explicit steps during pause;
- final-pose interpolation and unchanged readback-only observations;
- resumed accumulator behavior without old fractional debt;
- exactly one permitted final tick, no repeated/invalid reservation, and restart.

Sixteen UI lifecycle cases pass, including pause availability, Requested/
Draining suppression, Resume, failure cleanup and Leave from pause.

One actual Chrome 152.0.7977.82 hardware journey passes 17 recorded stages plus
drained Leave. It pauses while Q is held and the real winch motor is active.
The paused frontier, boat/cargo/rope observations, events and GameSession all
agree at tick **472**. For 800 ms, real W/F/R/E/H/B/Space input cannot move,
interact, reset, bank or enter the workshop. Recorded player, economy, boat
pose/velocity, cargo pose/velocity, rope state, simulation frontier and water
time remain unchanged. P resumes, with rope motor zero and target unchanged.
The run then verifies payout, intact towing, release, steering, moving-deck
walking, Reset, resize, pause at tick **899**, and Leave directly from pause.

The water clock remains **7.8853** in both recorded mid-expedition paused states.
On resume the queued interval remains within the existing two-tick bound.
LEGO terrain and inventory (48 material / 0 machinery) remain intact. There
are no browser exceptions or uncaptured GPU errors. No command/state injection,
banking scenario, screenshot, persistence journey or performance certification
is claimed. This native scheduling test and real browser journey do not replace
the still-required explicit fault, native UI, or full restore acceptance.

All build/test attempts passed. Native build r01 preceded the final scheduling
test and final-pose interpolation adjustment; r02 and the WASM/browser package
contain the final code. The prior SAVE-04 capture report remains historical
evidence for the earlier source version.

## Continue with the actual expedition archive

The Paused owner is now the entry point for coherent archive capture. While it
holds admission closed at the application boundary, capture the full existing
SessionRecovery/SVSC state and the corresponding physical state. Keep the whole
SAVE-04 requirement; do not replace it with a shore-only save or boat reset.

The next implementation must include:

1. A bounded versioned archive tying logical checkpoint identity/coverage to
   durable build/cargo root motions, rope endpoint/target state, water content
   and time, player state, and secured/banked cargo. Validate content/topology,
   identity correspondence and counts before loading. Ordinary CargoRecord
   position currently retains its logical location; dynamic observed motion
   must have one explicit owner in the archive, not two competing authorities.
2. Correct restore preparation: recompile the accepted build and fresh runtime
   shapes/handles, restore root motion and boat-local player state, reconstruct
   cargo once and its tow at the saved target, and neutralize transient input.
   Keep velocities for controlled semantic resume. Solver/contact history is
   not currently sufficient for bit-exact continuation.
3. Integrate the existing Linux/IndexedDB generation stores, independent world
   selection, serialized asynchronous writes and real archive validation. Use
   owned byte buffers for the WASM bridge; do not put whole-world data on the
   one-MiB ccall stack. Preserve known-world missing/corrupt-save errors.
4. Publish SessionRecovery retired-parent/fresh-child lineage atomically before
   new durable commands, and wire genuine typed storage acknowledgment to
   journal release/receipts. Existing RAM prefix-release is not durable proof.
5. Actual save/load controls and native/browser journeys while towing and after
   release/banking, then unfinished latching, Windows storage, world export/
   import, migration, durable compaction and the complete SAVE gates. A pause
   acknowledgment must never be displayed as saved or durably banked progress.

## Reproduce after relevant changes

```sh
nix-shell --run 'bazel build -c opt //tests:voxy_tests //:voxy_native'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="GpuPhysicsTest.*Attachment*:GpuPhysicsTest.BodyAndWinch*:GpuPhysicsTest.CombinedReadback*:GpuAuthoredShapes.Live*:GpuEventReadback.*:CoveMovement.*:CoveNavigation.*"'
node scripts/test_salvage_preview.mjs
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8'
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_COVE_PAUSE=1 VOXY_SMOKE_COVE_TERRAIN=1 VOXY_SMOKE_REPORT=/tmp/pause-browser.json VOXY_SMOKE_COVE_PLAYER=/tmp/pause-journey node scripts/smoke_integrated_wasm.mjs /path/to/packaged-web salvage-cove
```

Tested package: `/tmp/voxys-pause-web-r01`, containing `web/` plus the built
`voxy_wasm.{js,wasm,data}`. The private browser fixture's server/profile close
after the check. Existing user previews were not replaced.
