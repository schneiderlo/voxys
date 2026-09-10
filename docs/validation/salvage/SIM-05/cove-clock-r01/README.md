# Cove wave clock correction

The previous loaded browser journey advanced waves by 5 seconds during only
50 physics ticks (0.8333 seconds). The lifting cable then broke. That proves
a timing mismatch, not that timing was the only cause of the failed haul.
The original failure remains in `../../MECH-05/cove-roots-r01/`.

## Implemented behavior

- `CoveWaterClock` advances only for the physics tick included in a submitted
  cove frame. Repeated rendered frames and queue stalls spend no wave time.
  Preparation is copy-only; discarded frames retain the prior clock.
- Both native and browser cove profiles admit at most one physics tick per
  submission. The browser already used this limit. Native now uses the same
  explicit limit because the current shared wave texture represents one tick.
  Other experiences retain their existing scheduling configuration.
- The frame binds its owned physics incarnation, exact tick and wrapped wave
  phase through `PhysicsWorld::stageWaterGpuFrame`. Invalid phase, foreign or
  stale identity, unsupported batch size and changes after encoding refuse.
  Once opted in, encoding a later tick with an old stamp fail-stops before
  recording physics. The renderer produces the FFT at that phase before
  physics; analytical long swells consume the same phase in shader uniforms.
- Pause and its neutral maintenance ticks freeze wave phase. Restoring starts
  at the archived seconds, including its neutral settling tick. Rescue and
  dock service ticks remain frozen until Resume. Integer tick differences are
  taken before conversion; absolute u64 tick values never become float time.
- A discarded frame invalidates the FFT cache, so unsubmitted computation
  cannot be mistaken for a current texture. The application restores its
  unpublished water time through its existing frame guard.
- Read-only cove diagnostics now include `pause.waterTick`, the wave field's
  submitted tick. This is not a claim of completed physics. Paused saves still
  wait for the existing joined body/event/session completion boundary.

This is a bug correction with semantic save compatibility: no content geometry,
units, ownership, save schema or stored water settings change. Existing v1-v3
loads retain their recorded water seconds. Full SIM-05 remains open: general
catch-up needs one field per tick; GPU water velocity, water-relative drag,
spectrum/seed/frame identity, bounded CPU approximation errors and presentation
interpolation are not implemented by this correction. Player walking still
uses its existing separate stepping and needs the remaining SIM-04/ACT work.
No performance acceptance, general WaterField or full gate is claimed.

## Verification

Status: the scoped cove wave-clock correction passes. Full SIM-05 and gameplay performance remain open.

The first native journey passes 25 actual-control stages: purchase and launch
of the lifting rig, recovery of the 420 kg cargo, loaded return, durable reward,
process restart, refusal of a second payout and sailing at 1.7672 m/s.
The saved tick 1294 and restored tick 1295 both retain exactly 21.55 seconds
of water time. This native journey precedes the final fractional-accumulation
and near-wrap conversion refinements; its earlier clock header is retained in
`checks/cove-water-clock-playable-r01.hpp`. Do not label the later native binary
as the artifact used in that historical journey.

Browser r01 passes paid construction with 72 ticks / 1.2 seconds and 76 ticks /
1.26667 seconds. It then fails before boarding: Chrome's GPU process exits 133
and WebGPU reports a dropped external instance. Chrome also records database
write failures, while the tool sandbox independently reports `/tmp` EDQUOT.
A causal connection has not been established. Three inactive compiler caches
were moved off the quota-enabled tmpfs into workspace scratch, preserving all
outputs/profiles; the exact paths are in `checks/temporary-quota-recovery.json`.
Browser r02 uses the final frozen `web-r02` package. Wave phase remained at
one second per 60 submitted ticks and the cable stayed intact. However, its
obsolete driver reeled until cargo height exceeded -0.5 m, repeatedly pulling
an immersed load into the beam as the boat rolled. The craft overturned. The
agent stopped the owned test browser and preserved `operator-stop.json`;
this is a failed journey, not successful gameplay evidence. The native driver
had already been corrected to shorten to 4 m, tow partly submerged, then use
short hoists in harbor while stopping before 2.5 m. The browser driver now
uses that same strategy with unchanged delivery/rope/ownership assertions.
The old driver is retained in `checks/browser-delivery-driver-r02.mjs`.

A bounded timing observation of r02 found about 1 displayed frame/second,
1.2 ms CPU and 3.19 ms GPU, with no queue saturation. The document claimed
visibility/focus under existing focus emulation; the X11 window was mapped
and normal. This does not identify the precise compositor cause or establish
usable gameplay performance. Headless hardware r03 could not initialize a
WebGPU adapter; its failure is retained. Browser r04 returns to the available
normal X11 hardware profile with corrected controls and the same frozen game
package. It passes 29 actual-control stages: paid rig construction, hooking,
partly submerged return, harbor hoist, durable payment, actual reload, refusal
of duplicate payment and sailing away at 2.42526 m/s. The 1035 kg craft retains
paid part ID 35 and 96 material. The same banked 420 kg generator stays static.
Tick 656 saves and tick 657 restores with unchanged 10.9167 seconds of water
(the diagnostics round to six significant digits). Subsequent 90 physics ticks
advance the water by exactly 1.5 reported seconds. Browser errors are empty.
`browser-delivery-r04.json` and the stage summary retain the full result; its
isolated profile is `/tmp/voxys-startup-9XF1gI`. No scheduler or physics setting
was changed to claim a performance pass.

- Six clock cases pass in Bazel and actual JS-EH/Asyncify WebAssembly: slow
  frames/stalls, discard/retry, paused maintenance/resume, u64 restored tick,
  phase wrap, invalid/multiple-tick rejection, and drift-free stepping near the existing 1e12-second save limit. The initial 192-case run remains in `wasm-r01/`. The WASM shared suite has
  193 passing cases; its immutable executed runner is in `wasm-r02/`.
- The actual GPU skiff receives the same swell-induced velocity at the same
  staged phase when started at tick zero or `(1 << 60) + 600`. A different
  staged phase changes velocity. The test also verifies stale/foreign/nonfinite
  refusal, no mutation after encoding, and fail-stop on an unstaged next tick.
  This isolates analytical swells with a zero spectral texture; application
  journeys exercise the real FFT producer.
- The final Bazel selection passes all 69 cases: 42 cove/player, six clock,
  and 21 archive (the archive target is a valid unchanged cache hit). All six
  CMake clock cases and both native application build paths and the WASM
  application build pass. See retained logs for exact sources/runs.

No images were captured. This work does not replace the current user preview.

## Reproduction and retained failures

Run from the repository root inside `nix-shell`:

```sh
bazel test -c opt --jobs=8 //tests:cove_water_clock //tests:cove_save
bazel test -c opt --jobs=8 //tests:cove_player --test_filter=CoveMovement.WaterPhaseIsBoundToOwnedTickAndSurvivesLargeRestoreEpoch
cmake --build build-native-save-host --target voxy_native cove_water_clock_tests cove_player_tests -j8
ctest --test-dir build-native-save-host --output-on-failure -R '^cove_water_clock\.'
```

For WASM use `scripts/validate_session_transactions_wasm.py`, the SDK at
`/tmp/voxys-emsdk/upstream`, Node at `/home/modkin/.nix-profile/bin/node`, a new
output directory and `--exception-mode js --asyncify --expected-tests 193`.
The configured application build is `build-lego-wasm`, with
`EM_CONFIG=/tmp/voxys-emsdk/.emscripten`. Scratch for this change is
`build-cove-water-XHSMQGSh`; its `web-r02` freezes the browser package.
Run GPU checks and actual journeys sequentially.

The first GPU test tried to stamp a world before the first owned submission
created its frontier. The second read a snapshot before its asynchronous
completion arrived. The final test uses the real submission boundary and
bounded completed-evidence polling. Production acceptance and force thresholds
were not weakened. Both failed logs are retained.

## Next work

The native and browser haul/reload checks now pass within the artifact scopes
above. Continue D32 live root/cut integration and protected recovery. The new
atomic parent retirement primitive is tracked separately in
`../../MECH-05/parent-retirement-r01/`; it was added after the frozen browser
package and is not attested by this journey. Keep general WaterField, player
clock joining, normal displayed-frame performance, parent tasks and gates open.
Do not raise break limits, remove brick terrain or inject cargo motion to pass
future gameplay checks.
