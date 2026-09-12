# Launch waits for upload capacity — D43

**Two focused CPU cases and the existing real GPU backpressure case passed.**
Both final combined application builds passed. The actual native recovery
journey passed 42 stages, including the paid lifting-rig Launch; its shared
evidence is linked below. The final uncapped browser machinery journey also passed 28 stages, including
both Reverse/Disable Launches. The [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json) passed: **2,096 native cases passed, 3 skipped and 4 disabled**; terrain import **10 passed / 1 skipped** from cache. Bazel elapsed **1,132.896 seconds**; native test execution **1,125.548 seconds**. Publication remains pending. This is not a UX-01 gate pass.

A valid workshop launch now retains its prepared boat while the bounded GPU
shape-upload queue is temporarily full. The player does not need to click
Launch again for a temporary Busy/NotReady result. Shape limits, ownership,
costs, save format and all final launch eligibility rules remain unchanged.

## Reproduced failure and focused results

The triggering browser r03 run accepted real F9 input, then failed the first
reversed-propeller Launch after **63.702 seconds and eight stages**. The valid
kept design remained in Workshop with “Boat preparation is busy. Try Launch
again.” This was a failed journey. Its original summary, outer report and
runner log are retained under `browser-trigger-r03/`; their original paths and
error state are unchanged. The package manifest there belongs to that triggering
pre-D41/D43 build, not the final combined candidate.

| Check | Actual result |
|---|---|
| New scripted-resource Cove cases | **2 passed, 0 failures, 0 skips**, 0.747 seconds |
| Existing actual GPU eight-slot queue case | **1 passed, 0 failures, 0 skips**, 0.074 seconds on AMD Radeon 890M / Vulkan |
| Combined focused invocation | Both targets passed; 16.369 seconds including build work |

Raw `checks/focused-r01.log`, both target logs/XML and parsed `checks/results.json`
are preserved. The GPU log also retains the host's `XDG_RUNTIME_DIR` diagnostic;
the headless device initialized and the selected GPU test completed successfully.
No app window, screenshot or broader GPU matrix was run for these focused checks.

The first CPU case compiles an actual two-root Cove boat and scripts initialization
NotReady, an upload NotReady, one accepted shape followed by Busy, and a later
retry of only the missing shape. It compares untouched payload bytes/storage,
retains exact root keys/handles and proves Uploading is not Ready. An empty
payload vector after all handles are accepted remains valid while completion
is pending. The second case rejects malformed counts before any upload and
checks Capacity plus the valid-handle-with-GpuFailure edge. All accepted payloads
remain owned through Retiring and disappear only after the explicit scripted
resource poll. These are protocol tests; their fake poll is not GPU evidence.

The separate real `GpuAuthoredShapes` case fills all eight pending-operation
slots, verifies Busy preserves the ninth prepared payload, drains actual GPU
work and successfully retries. That existing test proves the resource contract;
the final real browser journey must still prove Application's use of it.

## Implementation and ownership

`prepareBuild` completes fallible CPU compilation/copies, installs the retained
launch candidate and returns Pending. The existing update path polls resource
completion before session preparation. `pollBuild` calls the small
`CoveRigidRoots::prepareShapes` helper, which uploads only roots with missing
shape handles. Refused Busy/NotReady values retain their prepared payloads.
The helper allocates no state, copies no payloads, performs no polling and
admits no bodies.

Every returned handle is stored before classifying an error, including the
real backend case that returns an inserted valid handle with GpuFailure.
Terminal errors cancel through the existing partial-owner retirement path.
Ready means every actual shape state is Ready, not merely submitted.
The existing joined physics frontier, player/space/harbor checks, atomic body
replacement, new-body observation and parent-death publication requirements
are unchanged. Logical design/stock is not published during temporary upload
waiting. No queue capacity or frame-in-flight limit was increased.

`preparation/review.md` records an independent source review with no actionable
findings. Its source-only wording is historical; the focused results above
were obtained afterward by root. `helper.patch` covers the helper, direct
physics header dependency and two tests. `application.patch` is localized to
prepare/poll callbacks so D41/D42 HUD changes remain intact. Historical baseline,
candidate and applied-helper hashes are retained. `source-scope.json` labels
the current live snapshot separately; the CPU/GPU targets do not execute the
Application launch callbacks or certify a later combined package.

The exact focused filters and integration contract remain in
`preparation/HANDOFF.md`. The final native/WASM builds and actual 42-stage native
recovery run are recorded once in [native guidance evidence](../recovery-guidance-r01/README.md).
That run used the current combined Application and accepted a paid lifting-rig
Launch before towing/delivery/restart/power. It is native integration evidence;
it does not establish the triggering uncapped browser backpressure path.

The final combined browser run passed **28 machinery stages in 84.663 seconds**
on frozen `build-cove-guidance-r01/web-r01`, after its five objective stages.
Real Reverse and Disable were each kept and launched through one ordinary
Launch action, with no driver retry, force-submit or state setter. Exact
11-part/1,035 kg design, 48 material, zero special machinery and no paid IDs
survived both refits and save/reload. This completes the specific browser
integration acceptance that failed before D43. The full outer report passed
with no browser, sample or console errors.

The exact browser summary, outer report, log and final package/source
identity are archived once in [driven Cove evidence](../../LOOK-01/driven-cove-r01/README.md).
No additional view or unchanged journey is required. The [joint D39–D44 required result](../../checkpoints/driven-cove-r01/checks/summary.json) passes against
final nine-presentation content; publication remains pending.
