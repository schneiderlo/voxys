# Driven Cove machinery and dock route — r01

**Native and browser integration verified, 2026-09-12.** This candidate follows
published checkpoint `411fa1cf`. The original native machinery journey passed
39 stages; the final combined D39–D43 browser run passed 28 machinery stages and
five objective-card stages. The final combined native mission passed 42 stages,
including delivery/save/restart and durable harbor power. Both recorded application
builds passed. The [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json) passed: **2,096 native cases passed, 3 skipped and 4 disabled**; terrain import **10 passed / 1 skipped** from cache. Bazel elapsed **1,132.896 seconds**; native test execution **1,125.548 seconds**. Publication remains pending.
This is not owner visual approval or completion of LOOK-01/ACT/game-production gates.

The change makes the propeller follow actual submitted propulsion and the
winch drum follow accepted changes in the attached cable's rest length. A lit
teal dock lane and orange boarding outline are intended to make the existing
route from spawn to boarding easier to read. The propeller guard and winch
frame stay fixed. No screenshots, image generation or repeated camera journey
are part of this checkpoint.

## Current evidence

| Check | Actual result retained here |
|---|---|
| Blender source and two strict cooks | Exports, author checks, saves and both cooks completed; Blender shutdown was interrupted, as explained below |
| Asset numeric verification | Final `verify-r04.log` passed; earlier comparison failures are retained |
| Independent asset/helper/driver source review | Completed with corrections recorded in `source-review.md`; no runtime claim |
| Focused mechanism/fixture checks | **13 passed, 0 skipped, 1.35 seconds**, in `checks/focused-r02-test.xml` |
| First focused attempt | Compile failed at an unbraced GoogleTest macro; **no tests ran** |
| First standalone GPU-pose attempt | Missing direct Bazel header dependency; **no tests ran** |
| Second standalone GPU-pose attempt | Built and ran one case; **failed**, because the test's green classifier incorrectly required zero red/blue after ACES |
| Corrected standalone GPU-pose check | **1 passed, 0 skipped, 0.118 seconds**, in `checks/gpu-pose-r03-test.xml`; only this affected case reran |
| First dock focused run | 5 executed: **4 passed, 1 failed, 0 skipped**, 1.927 seconds; only two total-draw expectations failed |
| Dock numeric readback and storage | Color/depth/hidden restoration assertions passed; **78 changed opaque pixels (28 teal / 50 orange), 7,760 marking bytes** |
| Corrected dock draw expectation | **1 passed, 0 skipped, 0.475 seconds**; only the affected case reran, numeric properties unchanged |
| Native/WASM application builds and package hashes | Final combined D39–D43 native/web-r01 builds passed; exact manifest retained as `checks/combined-package-r01.json`; earlier packages remain separate |
| First native journey | **Failed after 30.167 seconds** at the driver's exact-waypoint requirement; real boarding prompt was available and E was never pressed |
| Second native journey | **Failed after 71.95 seconds**: post-dock exact-waypoint requirement despite `workshop.canOpen=true`; also one surface-acquisition error retained |
| Third native journey | **Failed after 79.894 seconds** on a repeated approach after successful reverse refit; no process error lines |
| Fourth native journey | **Failed after 51.923 seconds** on real reel/drum phase mismatch; navigation, both refits and all propeller checks passed; zero process error lines |
| Fifth native journey | **Failed after 62.957 seconds**: ten control/motion comparisons passed, including exact reel/pay phase; final nonzero drum checkpoint precondition failed after cancellation |
| Final native journey, r06 | **39 stages, 11 phase checks passed in 68.056 seconds**; both stopped process logs contain zero error lines; exact save/restart ownership and neutral restored phase passed |
| First browser journey, r01 | **Failed after 237.308 seconds** when the owned Chrome process was deliberately stopped; 14 partial stages, measured 1 FPS visible/focused browser schedule |
| Second browser journey, r02 | **Failed after 10.032 seconds**, zero stages: real F9 was blocked by existing Cove/workshop shortcut returns |
| Third browser machinery attempt | **Failed after 63.702 seconds / 8 stages** at reversed Launch; temporary upload Busy was incorrectly rejected; original failure retained |
| First combined objective attempt | Failed at Accept before the machinery subjourney; cause unresolved, separate D42 evidence linked below |
| Final combined browser | **28 machinery stages passed in 84.663 seconds**, plus **5 objective stages in 3.033 seconds**; outer report passed with no browser/sample/console errors |
| Final combined native mission | **42 stages passed**, including paid Launch, delivery, restart and durable power; separate D41 evidence linked below |
| Full required suite and publication | **Passed** in the [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json); publication pending |

The 13-case result covers five phase-controller cases, three rigid-node placement
cases, four fixture admission/ticket/budget cases and one installed catalog
contract case. It measures **10,745,384 owner bytes and 82 color draws before dock
markings**. The actual final art with 64 large bricks plus the three palette
bricks fits at **92 placements and 149 color draws**. These numbers do not include
the dock mesh. Fixed requested storage remains **119,336 bytes**, within
the existing 131,072-byte reservation; the double guide mesh is included.
The 16 MiB owner, 48 MiB resident, 256 instance and 512 expanded-draw caps remain.

The first compile failure was `-Werror=dangling-else` around `if(role) EXPECT_EQ`
in `tests/test_fixture_registry.cpp`; braces fixed it before the 13-case run.
The standalone test initially lacked the direct `//src/game:rigid_prefab`
dependency for its new include. That dependency was added before the second
attempt. `gpu-pose-r02.log` records visible moving-region changes (456 rotor /
420 drum pixels), zero static/live-root differences and blank stale-generation
output, but its stationary-region assertion failed. Those partial measurements
did not constitute a passing pose test. Both compile failures and the failed
runtime result remain unchanged beside the corrected passing result.

The read-only review in `handoffs/gpu-classifier-review.md` traced the failure to
the test's mask: existing ACES maps linear green `(0,1,0)` to approximately
RGB8 `(148,228,89)`. The corrected classifier uses `G > R + 70` and
`G > B + 70`, as an existing test already does. Geometry, rendering and acceptance
thresholds did not change. The single corrected case passed with **87 rotor /
88 drum stationary pixels, zero changed stationary pixels, 456 / 420 moving
changes, zero static/live-body differences, and zero stale-generation pixels**.
This proves the bounded numeric pose case. Native application drive/dock/save
verification also passed in r06 below; final browser verification is recorded below.

The dock's first focused run compiled and executed all five selected cases.
Its three CPU contract cases and the mechanism full-scene owner case passed.
The dock GPU case failed only its encoded/submitted total-draw expectations:
it counted all eight admitted plates, although that fixed camera sees four.
Observed total was six draws: four visible plates plus two inlay draws. Root
changed the expected baseline to the actual culled baseline, retaining a positive
bounded baseline and exactly two added draws. The single affected-case rerun
passed in 0.475 seconds with zero skips and unchanged numeric properties.
`checks/dock-focused-r01-*` preserve the original failure and its passing numeric
opaque color/depth and exact hidden-restoration assertions; `dock-draw-r02-*`
retain the complete corrected pass.

## Combined application builds

Both native and WASM r01 builds succeeded. Clang additionally warned about a
reference reached through temporary `art->lods()` span syntax. The admitted
bundle owns the backing LOD vector throughout the draw; the vector was not a
temporary. Storing the non-owning span in a named local expresses that existing
lifetime and removes the new `-Wdangling-gsl` warning without changing phase or
draw behavior. Both r02 builds passed afterward. Final WASM compilation retains
12 pre-existing warnings in the Application translation unit. No CPU/GPU test
was repeated for this lifetime-expression clarification. Original r01/r02 logs
and `handoffs/wasm-span-review.md` are preserved.

Completed r02 builds produced package snapshots `native-r01` / `web-r01`, used
by the first two native attempts. The named Timeout change passed both r03 builds
and produced snapshots `native-r02` / `web-r02`; native journey r03 used that
frozen executable. Final Context lifecycle bookkeeping passed both r04 builds
and produced **`build-cove-mechanisms-r01/native-r03` / `web-r03`**. Native journey
r04 used that frozen native package and failed the drum phase check described
below. The accepted-rope baseline correction then passed both r05 builds and
produced **`build-cove-mechanisms-r01/native-r04` / `web-r04`**. Native r05/r06 use
that same frozen native executable; the driver-only checkpoint correction requires
no rebuild. All completed build logs and package manifests
`checks/{native,wasm}-package-r01/r02/r03/r04.json` are retained. Do not attribute
the lifecycle change to the older frozen executable. Binaries are not duplicated
here. The earlier native-r04/web-r04 mechanism-stage package hashes were:

| File | Bytes | SHA256 |
|---|---:|---|
| `voxy_native` | 47,201,856 | `4da95b277073f4e7bb2301875ee871af0bb513f1255612af4ffef47a14b9f7a8` |
| `voxy_wasm.js` | 99,225 | `ce2f8e7edf982cfe1ebc03f2e6996b2669495fbe40ef6726a66ab58363399438` |
| `voxy_wasm.wasm` | 7,278,990 | `ff1d06781c5268dc5d1a81e5c34d0e59f44f7a5ca1f1b98675edefbfcc5be615` |
| `voxy_wasm.data` | 87,364,450 | `e68fd82d3ea55260343834c7c15ec00b4b8e302784bef2e26eb996b64b8f7010` |

## Actual-control attempts

The first native journey reached feet `[4.68133,1.285,-52.3956]` with the real
`board` interaction available, but its driver insisted on approaching the exact
authored point and timed out without pressing E. Its only recorded stage was
`fresh-world`; this is **not a gameplay pass**. The application remained ready,
its failure flag was false, and its stopped process log recorded no GPU/runtime
error lines. The unchanged summary, final state, process and runner logs are in
`native/attempt-r01/` and `checks/native-journey-r01.log`.

Both drivers now follow the actual marked turn route `(6,-49.5)` then
`(4.5,-49.5)` and finish the final approach when the real board prompt appears.
Helm/dock approaches likewise finish at their real interaction prompt. Physical
E/click input and a confirmed mode/ownership transition remain required. This
driver-only correction changes no Application, physics, collision or camera
code and requires no rebuild; `handoffs/boarding-driver-review.md` records it.

The second native journey passed the actual forward/reverse rotor pairs and
both stopped checks, then failed after docking because it again demanded an
exact waypoint even though `workshop.canOpen=true`. It ended after 71.95 seconds
at `[4.35253,1.285,-51.3427]`. Forward/reverse checks each covered six ticks at
drive +1/−1 with wrapped error magnitude `2.22e-16`; stopped checks covered
nine/seven ticks with zero phase error. These are partial results, not a
completed journey. Its original summary, final state and process log are in
`native/attempt-r02/`, with the runner log in `checks/native-journey-r02.log`.

The second process also logged **`Failed to get current surface texture:
status=1` at process time 00:00:29.604**. That original error is preserved without
filtering or suppression. The record does not
describe this attempt as GPU-error-free. Both drivers now stop the dock-to-
Workshop approach on the actual `canOpen` state, then physically press B/click
and confirm Workshop opens. No Application/physics/camera change or rebuild is
made for that driver correction. At that point, native/browser save/restart
completion and the full required suite were still pending.

Source review identified native status 1 as the pinned wgpu-native v22.1.0.5
**Timeout**, whose native Success is 0; the separate WASM API's named Timeout
has value 3. The new branch compares the named API enum, warns once per
consecutive timeout, releases any returned texture and skips the frame before
renderer/physics tickets. Successful reacquisition logs recovery and clears
the warning flag. Genuine acquisition-error branches and both journey error
scanners are unchanged. This is a recoverable-frame diagnostic correction,
not a blanket removal of error checks. `handoffs/boarding-driver-review-r02.md`
records the pinned API distinction. The independent
`handoffs/surface-timeout-review.md` also requested that the warning flag follow
both Context moves and reset on shutdown. Those assignments are applied in
`src/gpu/context.cpp` and included in both completed r04 builds.

The third native attempt's frozen r02 executable recorded a real timeout warning
at **64.072 seconds**, followed by successful reacquisition at **64.073 seconds**,
and its strict process scanner found zero error lines. This is evidence of that
recoverable path, not a complete gameplay pass or a forced-timeout test of the
later lifecycle change.

Native r03 passed forward/reverse/stopped phase pairs and completed a free
reversed-propeller refit, then failed a subsequent walking approach at
`[4.42838,1.285,-51.4114]` after 79.894 seconds. Its unchanged evidence is in
`native/attempt-r03/` and `checks/native-journey-r03.log`. Source comparison found
that the mechanism driver's held-movement helper lacked the post-release fresh
observation wait already used by the proven delivery helper: with 10 Hz samples,
the next movement could reuse an old position. Both native/browser helpers now
wait for a newer player tick after releasing input; intermediate boarding steps
also honor the actual board prompt. A bounded `failedApproach` trace is added
for any next failure. This was a candidate explanation/fix, not proof by source
inspection alone. Native r04 subsequently completed all approaches, both free
refits and its third boarding. No physics or camera change is credited, and
that navigation progress is not a complete journey pass.

Native r04 then exposed a **real Application rope-adapter defect**. It failed
the first reel-in comparison after 51.923 seconds. Accepted rope length changed
from 7.223200798034668 to 7.08986759185791 m, so the expected drum change was
0.4761900220598493 radians; observed phase advanced only 0.23809501102992464.
Wrapped error was −0.2380950110299246, far outside the unchanged tolerance.
Its 24 recorded stages include both free refits and all forward/reverse/disabled/
stopped propeller pairs. The original failed summary, final state, process log
and runner log are preserved in `native/attempt-r04/` and `checks/`. The strict
process scanner recorded no error lines. No reel, save/restart or full journey
success is inferred from those partial checks.

Root found that the Application omitted the last accepted rope sample while a
new motor command awaited acknowledgement. The phase helper interpreted the
missing sample as detachment and reset its baseline, dropping the first two
reel ticks. The correction retains the previous accepted, alive sample of the
same current handle while motor acknowledgement is pending. Real detach,
replacement handle, pause and monotonic sample protections remain. Independent
source review found no remaining issue; the unchanged review is preserved in
`handoffs/drum-command-baseline-review.md`. Both r05 Application builds passed.
This changes runtime integration, unlike the earlier driver navigation corrections.

Native r05 then passed **all ten recorded control/motion comparisons**, including
reel-in `−0.1333332061767578 m` / `+0.4761900220598493 rad` with zero wrapped
error, and pay-out `+0.1333332061767578 m` / `−0.4761900220598493 rad` with
`2.22e−16 rad` error. Stop, release, Workshop and pause freeze assertions also
completed. The accepted reel/pay changes canceled exactly, however: its final
drum phase was zero. The driver correctly refused to call a zero-to-zero reload
a neutral-phase reset proof. It failed after **62.957 seconds**, before saving
or restarting; 33 stages and zero process error lines are preserved under
`native/attempt-r05/`. This is partial motion evidence, not a completed journey.

Both drivers now apply one additional real Q pulse only if the accepted final
drum phase is too close to zero, then recheck the same signed cable/phase
relation before release and final freeze. No state setter, save injection,
phase tolerance relaxation or runtime change is involved. Native r06 used
the same frozen package and passed as described below. The final browser result appears below. The original failed r05 artifacts are unchanged.

The final **native r06 passed 39 stages and 11 phase checks in 68.056 seconds**.
The additional conditional reel pulse ran through real Q input and passed the
same cable relation: `−0.1333332061767578 m` produced
`+0.4761900220598493 rad` with zero wrapped error. Forward/reverse/stopped,
reversed-setting and disabled-setting phase pairs passed; both settings refits
were free. Actual hook, reel, pay, hold and release, Workshop freeze, pause
freeze, F10 save and a new process reopening that world all passed. Both
stopped-process scanners found zero error lines.

The full 11-part/17-connection owned design, 1,035 kg mass, 48 material, zero
special machinery, no paid IDs and disabled/reversed propeller settings survived
the restart exactly. The saved part and connection digests match the restored
archive; presentation rotor/drum phase and drive restarted neutral after a
nonzero saved-phase checkpoint. Each stage carries later actual GPU/physics
completion evidence under its exact body/build/incarnation identity. Dock
markings, seven installed presentations and readable native HUD assertions
passed in the same journey. `native/attempt-r06/` preserves both process logs,
observations, the 18,171-byte physical save and unchanged full summary; its
`archive-sha256.json` pins copied bytes. The main summary pins the final native
binary. This is the only completed native journey for this checkpoint; no
additional native run is required by this record.

## Browser attempt and scheduling evidence

Browser r01 used frozen `web-r04` and was deliberately stopped after **237.308
seconds**. Its raw journey and outer startup report both retain the actual
`Chrome connection is closed: Runtime.evaluate` failure. Fourteen recorded
stages include initial forward/reverse/stopped phase checks, the free reversed
refit and reversed-drive check, and the free disabled refit. It stopped before
the next boarding after disabled Launch; disabled-drive, cable, pause and
save/reload completion are not claimed. The corrected pacing review records this exact partial scope from the primary
stage list.

A single read-only timing probe reports a visible, focused AMD RDNA 3 hardware page,
**0.99935 FPS / 1,001.50001 ms current frame interval**, 1.100000001 ms CPU
submission and **3.758532 ms GPU frame execution**, with queue depth 1 of 12 and
zero pacing skips. This supports a browser/window frame-scheduling restriction;
it does not identify its operating-system cause, establish constant 1 FPS across
the whole run or establish a GPU throughput limit. The approach probe retains actual player/boat/readiness observations.
No screenshot or state setter was used.

Both probes, the unchanged failed summary, outer startup report and hashes are
under `browser/attempt-r01/`; `checks/browser-journey-r01.log` and
`handoffs/browser-pacing-review.md` retain the original runner/review. The next
browser journey physically presses the existing **F9** uncapped control and
checks its read-only status at initial load and after reload. Native runs
already use the existing `--uncapped` option. This changes driver input only:
at that point only driver input changed, with no acceptance relaxation.

Browser r02 then failed after **10.032 seconds with zero stages** because the
actual F9 uncapped input was not accepted. The ordinary key edge was valid;
source inspection found that the existing Cove return in
`Application::handleKeyboardShortcuts` occurred before the F9 handler, as did
the Workshop return. Root relocated that same F9 toggle immediately after the
null-input check and removed its lower duplicate. No other binding, renderer,
simulation or save behavior changed. Raw failure, outer startup, runner log
and hashes are retained in `browser/attempt-r02/` and `checks/`.

The F9 fix subsequently passed both r06 Application builds. Native motion acceptance above belongs
to the earlier exact native-r04 package, whose command-line uncapped mode already
worked; a full native motion replay is not required or claimed solely for this
F9 routing correction. The next browser attempt accepted F9 but exposed the temporary shape-upload
rejection described below. These observations do not certify product frame-time or visual quality.

## Final combined browser and source distinctions

Browser machinery r03 accepted real F9, then failed its first reversed Launch
with “Boat preparation is busy. Try Launch again.” after **63.702 seconds and
eight stages**. Its unchanged summary, outer report, runner log and package
manifest are under `browser/attempt-r03/`. D43 corrected Application preparation
to retain the compiled candidate through Busy/NotReady and retry missing shape
uploads after the resource owner polls completion. No capacity, ownership,
readiness or final launch predicate was relaxed. The [launch queue evidence](../../UX-01/launch-queue-r01/README.md)
records two passing CPU protocol cases and the real eight-slot GPU retry case.

The first later **combined D42 objective** attempt failed its Accept click before
starting the machinery subjourney. Its cause is unresolved; it is separate from the mechanism results. The later
passing trace records one trusted click forwarded once. Exact evidence belongs to the
[browser objective record](../../UX-01/objective-card-r01/README.md).

The final combined browser r02 used frozen
`build-cove-guidance-r01/web-r01` and passed **five objective stages in 3.033
seconds**, followed by **28 machinery stages in 84.663 seconds**. Its outer
report passed, with `browserErrors=[]`, `sample.errors=[]` and no console error
entries. The unchanged machinery summary, full outer report and runner log are
archived under `browser/accepted-combined-r02/` and
`checks/combined-browser-journey-r02.log`.

Actual forward/reverse/stopped rotor behavior, a free reversed-propeller Launch,
its reversed drive, a free disabled-propeller Launch and stopped output all
passed. Each Launch used its normal single action; no automatic driver retry
or state setter hid upload rejection. Actual generator hook/reel/pay/release,
Workshop/pause freeze, save/page reload, neutral restored phases and exact
exported design/stock/settings passed. The reel sample changed rest length by
**−0.2666664123535156 m**, pay-out by **+0.1333332061767578 m**; both satisfied
the unchanged signed phase relation. The saved drum phase was nonzero
(0.4761900220598492 radians), and reloaded rotor/drum/drive were zero.

The final browser retained 11 parts, 1,035 kg, 48 material, zero special
machinery, no paid IDs and the disabled/reversed propeller. Its exact exported
blueprint is 1,887 bytes, SHA-256
`c9b5b257833842138048f27f4d3cc6113434e6ec7d4a2705c1d3e3fee85e2e2d`.
Every recorded state includes later actual GPU/physics completion under matching
body/build/incarnation identity. Dock marking draws were exactly two outside
Workshop and zero inside; owner storage was exactly 10,753,144 bytes. Seven
presentation bindings remained installed. These numeric/runtime checks do not
constitute owner approval of the appearance or a new per-frame shadow matrix.

The earlier 39-stage native machinery result belongs to its exact
`build-cove-mechanisms-r01/native-r04` executable. Later F9, D41/D42 guidance and
D43 launch changes were not retroactively tested by that run. The final combined
`build-cove-guidance-r01/native-r01` instead passed the distinct 42-stage
[mission/delivery/restart/power journey](../../UX-01/recovery-guidance-r01/README.md).
No unchanged native machinery replay was performed. The final combined source
review is `handoffs/integrated-review-r01.md`; its pre-run pending wording is
historical. Both final builds and full package entries are preserved in
`checks/combined-*`.

| Final combined file | Bytes | SHA-256 |
|---|---:|---|
| Native executable | 47,223,048 | `f069239c46e25b545c258fa1f03c461b7d8e919cb5a8875bb34bd7b5f3ee9936` |
| WASM module | 7,279,865 | `a47b46fa6bbf547db3d52b65e579e90215ee2699edafcddb9512830f09714600` |
| WASM data | 87,364,450 | `e68fd82d3ea55260343834c7c15ec00b4b8e302784bef2e26eb996b64b8f7010` |

The [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json) now passes against final nine-presentation source/content. The later
[cargo-art package and old-save continuations](../cargo-art-r01/README.md) remain
separate from the earlier package identities above. Publication and wider game gates remain open.

## Player behavior and ownership

At the helm, forward/reverse input drives the rotor in the corresponding
direction. Stopping or disabling the propeller stops its rotation. Changing
Propeller Reverse in Workshop reverses this mapping. Each successfully
submitted 60 Hz tick advances the rotor by
`4π × effectiveDrive / 60`: two visual revolutions per second at full drive.
This is presentation speed, not a physical shaft-RPM measurement.

Hook/reel/pay/release use the existing cable controls. The drum advances by
`−ΔacceptedRestLength / 0.28` around canonical +X. The 0.28 m radius is the
authored cable-wrap centerline, not its 0.298 m outer surface. Input without an
accepted length change cannot spin the drum. First attachment, replacement
generations and a restored owner establish a baseline without rotation.

Phases are ephemeral presentation state. Prepare/submit/commit ownership prevents
discarded work from consuming ticks; monotonic rope watermarks prevent old handles
and pre-pause observations from rebasing the current rope. Stop, pause and Workshop
freeze phase. Reload starts neutral. Save schema, canonical identities, mass,
collision, inventory and propulsion/cable physics do not change.

Each LOD has exactly two mesh-bearing root nodes: `voxys_mechanism_static` and
either `voxys_propeller_rotor` at `(0,0,0)` about canonical +Z, or
`voxys_winch_drum` at `(0,0.12,0)` about canonical +X. No hierarchy, skin, clips
or animation channels are introduced. Basis 12 maps these to source axes −Z/−X.
The renderer rotates only the named node after its pivot translation, retaining
the live body's exact generational handle and the ordinary grid/root/basis
composition. The same transforms feed existing color, depth and shadow paths.
Legacy art and prototypes receive no mechanism pose. Invalid roles, nonfinite
angles and capacity refusals leave output/ticket ownership untouched.

## Dock surface and route contract

The dock candidate derives a bounded mesh from the original installed fixture's
upright canonical dock plates and navigation. It excludes boat/cargo/prototype
membership and never uses the mutable workshop boat as its surface. The route
steps toward boarding, moves sideways early, then follows the boarding-side lane;
it clears static obstacles using the existing 0.30 m player radius.

Lit teal lane surfaces and an orange socket-surrounding pad lie 1.5 mm above the
real plate tops. Individual molded panels use 40 mm edge margins, respect recessed
cross channels, and exclude canonical socket projections with 20 mm clearance.
The independent all-LOD flat-patch measurements permit ±0.488 m X / ±0.486 m Z;
the candidate uses the stricter ±0.460 m bounds. Unsupported rotations,
disconnected plates or a blocked route refuse atomically.

One rigid mesh shares the existing fixture owner and opaque-before-water path.
It receives scene shadows, does not cast sun shadows, and is omitted from
Workshop/palette/inspection. It is charged as actual asset storage before
admission, with a 16,384-byte bound. Source arithmetic gives **98 vertices,
48 triangles / 144 indices and two draws**; the executed fixture check confirms
**7,760 bytes** (7,056 vertex + 576 index + 128 material). The inferred combined
full-Cove owner is **10,753,144 bytes**. The first native attempt subsequently
observed that exact reservation, 7,760 marking bytes, two marking draws and
84 total draws in its healthy final state; this partial observation does not
make its failed journey a pass.
The authored journey requires exactly two
submitted marking draws outside Workshop, zero inside, and total reservation
`10,745,384 + dockMarkingGpuBytes`. Those application runtime assertions passed throughout native r06, including
Workshop hiding; final browser verification is recorded below. The preserved dock
handoff gives the exact admission/ticket API.

## Assets, provenance and reproduction

Only the existing propeller and winch presentation bindings are replaced. There
remain exactly seven presentations; canonical selectors stay unchanged.

| Part | Installed presentation | Cook-manifest SHA256 |
|---|---|---|
| Propeller | `data/salvage/mechanisms/r01/propeller` | `61e53b6fb936e49d74ea7c505b1aadbf0d5955c8ff5944b22f2c59235b4e57b8` |
| Winch | `data/salvage/mechanisms/r01/winch` | `d86f4f0df012234b720f01a3ca13338a65273aa9819018215119e1a5e2162bc5` |

The source canonical manifest selectors are propeller r09
`ec2d5a210f08dab4ecf34d599a13fea1d2883e2e0559859fae353ec802e0e2ee`
and winch calibration r04
`3d497e01fe01881858a29327d8187d4a95b150cba8fed6dcc9b5a7b020e40923`.
Fresh visual identities are `voxys-toy-art-v1`, version 1, counters 801–803 and
901–903. Canonical gameplay metadata, LOD identity/basis and 200/60/0 thresholds
are exact. `assets/presentation-additions.json` preserves the selected records.

The accepted corrected propeller and toy winch shapes are retained. The sole
intentional rest-appearance change is an orange material segment on the existing
winch flange rims. Its 68 vertex splits add 4,896 GPU bytes. Propeller stays at
238,752 bytes; winch becomes 691,344. All-angle blade/guard radial clearance
remains at least 13.994 mm; fixed arms/bosses retain 25/5 mm axial gaps. Winch
flange/cheek gap is 10.00002 mm and rotation preserves its axial separation.
Cooked swept bounds stay inside each existing part envelope.

Blender **5.2.1 LTS, build 9e2066aef7ef** finished both exports, author checks and
editable `.blend` saves, then hung during sandbox PulseAudio shutdown. That
completed process was interrupted with exit **130**; this was not a clean
Blender exit. Thumbnails were disabled. Both strict `salvage-rigid-v1` cooks
passed using the existing converter and C++ sidecar validator.

`assets/candidate-report.json`, both per-part provenance records and cook
manifests retain exact source/payload hashes and numerical measurements.
Large `.blend`, GLB and VMESH files are not duplicated here; they remain in the
installed asset directories. Original metadata correction and initial numeric
comparison failures are explained in `source-review.md` and preserved raw logs.

To reproduce authoring, run from repository root into a new output directory:

```sh
PYTHONDONTWRITEBYTECODE=1 /snap/blender/current/blender \
  --background --factory-startup -noaudio --threads 2 --python-exit-code 1 \
  --python tools/salvage_assets/author_cove_mechanisms.py \
  -- --output-dir <new-output-directory>
```

For each `propeller` / `winch`, inside the project Nix environment:

```sh
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar <new-output-directory>/<part>/source/<part>.gameplay.json \
  --sources <new-output-directory>/<part>/source \
  --output <new-output-directory>/<part>/cooked \
  --converter build-native-save-host/bin/gltf_vmesh_tool \
  --validator build-native-save-host/bin/gameplay_sidecar_tool
```

The installed recipe loads installed `author_cove_propeller_art.py`,
`author_cove_machinery_art.py` and their shape/material helpers. Per-part
`inputs` pin these dependencies, including `metric_materials.py`; no scratch-only
import path is required. No recook was performed to assemble this evidence.

## Remaining validation and evidence layout

`driver-contract.md` specifies the single combined actual-control journey and
commands. It requires correct completed-frame phases, actual free reverse/disable
refits, reel/pay length observations, pause and exact physical save/restart, with
the dock assertions included in the same run. It does not repeat construction,
cargo delivery or a camera matrix. Keep `VOXY_MESH_CAPTURE_DIR` unset and browser
screenshots disabled.

`checks/` contains raw compile/test logs, completed mechanism XML/summary, the
initial five-case dock result, its corrected single-case pass, completed native/
WASM build logs and frozen package manifests. `dock/` retains the original source arithmetic.
`assets/checks/` contains unchanged author/cook/verifier logs. `assets/` holds
the candidate report, selected bindings and provenance/manifests. `handoffs/`
preserves original preparation notes; their earlier pending status remains
historical. This README's table is the checkpoint's current status.

Pending results must be appended from actual finished runs with package/source
identity, exact counts, failures/skips and paths. The current evidence does not
establish user-approved appearance, physical
shaft dynamics, general rigs, an engine network, cable-spool capacity, new harbor
gameplay, performance on a new hardware tier, or completion of the game plan.
