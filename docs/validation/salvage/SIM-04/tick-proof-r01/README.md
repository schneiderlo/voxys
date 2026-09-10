# Confirmed GPU ticks for expedition physics

2026-09-09. Scoped SIM-04 work after physical sailing/dock collision. This adds
a real timing boundary for later cargo operations; it does not implement towing,
award salvage or complete SIM-04. No screenshots or gate commit.

## Contract and implementation

`PhysicsWorld::tickFrontier()` returns an unavailable result for CPU/legacy
execution. The GPU backend activates tracking on its first owned submission
preparation. Its pool identity is the incarnation. `baseTick` defines the start
of the tracked interval; no evidence is asserted for earlier execution.

Within that interval, the public record separates scheduled, encoded, submitted
and completed ticks. Encoding alone changes neither submitted nor completed.
Actual `submitGpuSubmission` advances submitted and records its real shape-owner
queue serial in a fixed eight-entry ledger. Each nonempty encoded batch copies
128 bytes of GPU counters into one of eight reserved readback slots (1,024 bytes
total, charged to persistent GPU storage). Completion requires both:

1. The shape owner has certified the submission's queue completion and GPU
   error scopes, in order, for the same incarnation.
2. The readback's GPU tick counter matches the expected batch-final tick, with
   no sticky authored admission or terrain-evaluation failure.

Only a contiguous confirmed ledger prefix advances completed. Discard after
encoding fails the world without certifying the abandoned tick. Repeated encoding
within the same ticket is refused. Restart gets a new incarnation and zero
frontiers. Idle owned worlds execute their GPU clock instead of inheriting the
legacy host-only sleeping-world shortcut.

Scheduling bounds total queued ticks to twice `maximumCatchUpTicks`; pending
host ticks separately retain their existing catch-up cap. Submission preparation
returns Busy before encoding if its proof readback/ledger has no capacity.
The application reports all 64-bit frontier values as JSON strings and stops
encoding raw physics once both cove bodies have retired. Camera/deck pose
observation remains the independent one-packet path from the dock milestone.

A no-progress guard fails after five accumulated active polling seconds. Each
poll interval contributes at most 0.25 seconds, allowing a suspended browser tab
time to deliver queued callbacks on resume. Progress means new proof data or
completion of a tracked batch's fence, not unrelated empty rendering submissions.
This guard is implemented but deliberate GPU-hang and hidden-tab timeout tests
remain part of full SIM-04/QA acceptance.

The owned expedition accumulator uses exact double `1/60` seconds when the
configured GPU duration is its nearest-f32 60 Hz value. The previous denominator
lost a boundary tick at 144 FPS: two seconds yielded 119 ticks. The solver keeps
its existing f32 duration; legacy scheduling remains unchanged.

## Verified evidence

`native-tests.log`: eighteen selected cases pass, including the prior sailing,
fixed-dock impact, clear-water control, static admission/reuse and calm-water
stability tests. The new tests prove encoding/submission/completion separation,
pause polling without invented time, discarded encoded work remaining unconfirmed,
and restart identity. A real GPU counter test runs two seconds of presentation
updates at 30, 60 and 144 Hz; each completes exactly 120 ticks. The retained first
cadence failure records the 119-tick defect at 144 Hz.

`browser.json`: hardware Chrome 152 / Radeon 890M passes boarding, helm use,
sailing, steering, deck follow, observed boat recovery, resize and drained Leave.
Every recorded stage checks ordered counters and the queue bound. This cove's
configured limit is two ticks; arrival reports submitted/completed 180/178,
steering 753/751, and resize 772/770. No browser exceptions or GPU validation
errors occur. This is a control/timing check, not a performance or visual gate.

Both application builds, shared shader synchronization and whitespace checks
pass. The exact tested browser package hashes are recorded. No image workflow
was run; the future shared diagnostic suite count was maintained only.

## Remaining integration — do this before cargo rewards

Completed here means proven execution of that batch and the checked core status.
It does not imply that every pose, contact, attachment event or query packet has
been delivered to GameSession. Those packets retain independent ticks. Add a
bounded owner that joins required pose/event evidence to this frontier, checks
missing/overflow/error packets, and schedules mutations strictly after submitted
work. Preflight per-tick event readback capacity before encoding; its existing
checked path currently fail-stops on exhaustion. Test delayed/out-of-order
callbacks, missing evidence, timeouts, pause and actual device loss.

Do not call `GameSession::advanceOneTick()` after a completed GPU tick to publish
ready topology: that CPU test driver could activate a revision retroactively.
The current cove still uses private scene identities and a rejecting campaign
preparation adapter. SIM-06 must supply real future-tick prepare/commit/discard
integration. SIM-05 still must supply tick-specific WaterField state; the current
boat samples the render-produced wave texture across catch-up ticks. Player
walking still has its temporary local clock. These are open requirements, not
accepted exceptions. Continue toward real towing/cargo through SIM-04–08.

Reproduce with the configured dependency environment:

```sh
nix-shell --run 'bazel build -c opt //tests:voxy_tests //:voxy_native'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter=GpuAuthoredShapes.Live*:CoveMovement.*:CoveNavigation.*'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8'
```

Package `web/` with the three built `voxy_wasm.{js,wasm,data}` files. After a
relevant gameplay/timing change, the existing control check can be repeated:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/cove-ticks.json VOXY_SMOKE_COVE_PLAYER=/tmp/cove-tick-journey node scripts/smoke_integrated_wasm.mjs /path/to/package salvage-cove
```
