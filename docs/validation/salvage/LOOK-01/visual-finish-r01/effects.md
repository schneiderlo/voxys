# Cove movement effects — VIS-04 component

Implementation, focused checks and the **eight-stage actual browser journey have passed**. The joint mandatory suite is still running. This page records the bounded effects component; it does not mark the full VIS-04 or LOOK-01 gate complete. The containing [visual-finish record](README.md) owns final builds, visual review, suite results and publication.

## Player-visible behavior

The skiff leaves short foam patches only while it travels through water. An enabled, submerged propeller produces foam according to its accepted effective drive, including reverse. Entering water produces a splash; a rising wet surface sheds droplets. An exact paired physical impact can produce surface splash or dry dust. Pause and workshop stop aging and emission. Rescue, changed physical incarnation and accepted replacement clear stale effects.

These are cosmetic effects. They do not change a force, collision, part, cargo, inventory value, reward or save byte. Surface runoff does not represent a pump or sealed compartment. No second-job runtime is activated.

## Source and integration contract

- [`CoveEffects`](../../../../../src/game/expedition/cove_effects.hpp) owns fixed particle state. Its implementation validates the complete input packet before mutation; refusal only increments a diagnostic counter.
- [`CoveEffectsPath`](../../../../../src/render/cove_effects_path.hpp) owns the instance/uniform buffers and cached bindings. [`cove_effects.wgsl`](../../../../../shaders/cove_effects.wgsl) draws the bounded overlay.
- [`Application`](../../../../../src/app/application.cpp) copies contact observations in `updateCoveBoat`, records effective drive only after successful submission, publishes motion sources after the player accepts the coherent presentation packet, and draws after successful final water composition.

`Inputs` contains a nonzero physics incarnation, one completed tick, a running flag, up to 40 motion sources and 32 copied impacts. A source carries its durable world/owner identity, full body index/generation and Boat, Cargo or Player kind. Player alone has no physics body. The first source packet binds the world namespace. Another world, stale tick, bad number, duplicate source or unpaired event refuses atomically.

Boat roots, cargo, player collision and the presented camera must share the accepted tick, and the existing ordered physics event consumer must have reached it. Local points use metres in the Cove frame; velocities use world axes. Authored point velocity is root-origin velocity plus angular velocity crossed with the rotated point offset. The 32-entry command history requires exact incarnation, tick and body identity; missing history suppresses driven foam instead of using a newer command.

For `ContactHit`, the adapter requires the exact current source handle and matching pose tick. `AuthoredBodyFrame::rootPoint` converts the chosen COM/principal-body local anchor into the authored root frame before rotation and translation. The anchor already uses metres. Reaction normal is `-normalAtoB` for source A and `normalAtoB` for source B. The other body and both feature IDs remain in the deduplication key. Unknown, retired or stale pairs are discarded and counted; no pose is guessed.

Water entry uses actual accepted displacement as well as current velocity. This matters when Swimming clamps the player's endpoint vertical velocity to zero. `boat.effects.playerEntrySplashes` counts only sprites actually admitted from a Player crossing, excluding boat/cargo/impact splashes and capacity drops. This is a read-only observation, not saved state.

## Bounds and resource lifetime

| Resource or work | Bound |
| --- | ---: |
| Shared wake, foam, splash, runoff and dust slots | 512 |
| Source samples / copied impacts per packet | 40 / 32 |
| Accepted command history | 32 entries |
| Maximum catch-up gap before reset | 15 ticks at 60 Hz |
| Maximum particle lifetime | 2.6 seconds |
| Instance buffer | 32,768 bytes |
| Uniform buffer | 560 bytes |
| Total requested GPU storage | **33,328 bytes** |

Emission, aging, source tracking and sorting use fixed storage without frame-time container growth. Saturation drops new cosmetics and preserves live particles. Duplicate ticks never replay emission. A gap above 15 ticks clears transient state and establishes a fresh baseline; it does not create a catch-up burst. The allocation claim follows the code's storage structure; the tests do not install a global allocation probe.

The renderer adds no texture. It borrows the existing water displacement/sampler and immutable final visible radial-depth view, retaining their WebGPU references in a cached bind group. Changed depth/water handles cause rebinding; ordinary frames do not create another group. The Cove owner allocates the effect buffers once. Each active/candidate/retiring fixture conservatively reserves the 33,328-byte dependency inside the existing 16 MiB owner and 48 MiB aggregate limits. The fixture does not allocate duplicate particle storage or report it as asset payload. Existing frame tickets govern retirement; borrowed textures are released, never destroyed by this component.

## Rendering and limits

One fixed six-vertex topology draws the live instances back to front with premultiplied alpha. Surface foam uses the same two FFT layers and four analytical swells as the current water, with a bounded three-step horizontal inversion. The CPU's strongest-24-mode water sample gates cosmetic emission only; it is not the authoritative hydrodynamic surface or a newly certified exact water query.

The shader samples final radial depth and writes no depth. It matches Cove ACES exposure and sRGB output, without grain or vignette. Air sprites below the water are clipped or attenuated using the existing bounded underwater approximation.

**The overlay blends after tone mapping.** It is not linear HDR transparency, temporal reconstruction or correct refraction of airborne effects through a water interface. Deep impacts are suppressed rather than sprayed through metres of water. General debris, arbitrary fracture art, pumping and broad physics-event guarantees remain outside this component.

## Executed checks and retained failures

The [component summary](effects/component-summary.json) contains exact source hashes and raw result paths. Ten distinct CPU cases are covered: the original nine passed together; one new crossing case passed separately, then the same case passed again after player-only attribution was added. This is **not** a claim that all ten ran together on the final source.

| Actual run | Result | Evidence |
| --- | --- | --- |
| Original CPU pool cases | 9 passed, 0 skipped; 2.900 s Bazel | [Log](effects/checks/cpu-r01.log), [XML](effects/checks/cpu-r01-test.xml) |
| New Swimming endpoint case | 1 passed, 0 skipped; 2.852 s Bazel | [Log](effects/checks/cpu-crossing-r03.log), [XML](effects/checks/cpu-crossing-r03-test.xml) |
| Same case with player-only attribution | 1 passed, 0 skipped; 2.743 s Bazel | [Log](effects/checks/cpu-attribution-r04.log), [XML](effects/checks/cpu-attribution-r04-test.xml) |
| Numeric effects renderer case | 1 passed, 0 skipped; 3.517 s Bazel | [Log](effects/checks/gpu-r01.log), [XML](effects/checks/gpu-r01-test.xml), [Properties](effects/checks/gpu-r01-summary.json) |

The original CPU cases check travel versus command-only wake emission, enabled/reversed/dry propeller foam, single crossing and runoff, stale/malformed event atomic refusal, contact identity/deduplication, pause, capacity and expiry, generation/epoch/gap resets, and invalid sources. The added case proves descending motion still emits when Swimming clamps velocity, a repeated or stationary mode change does not emit, and a boat splash cannot increase the player counter.

The GPU case measured **534 visible pixels**, **301 displaced surface pixels**, **17.366212 pixels** of surface centroid movement and **zero validation errors**. It also proves complete depth occlusion, exact preservation of the R32 depth contents, live water-height response, invalid-input refusal, cached binding replacement, abandoned encoding and submitted-resource retirement. No image was written for these tests. Later explicit iterator casts and encoded-observation clearing did not change the shader; that GPU case was not repeated.

The [failed CPU preflight](effects/checks/cpu-crossing-r02.log) could not reach the Nix daemon from the sandbox. It ran no compilation or test. The host retry passed. Historical results and source snapshots remain intact; no failed runtime is being relabeled as a pass.

Independent read-only reviews found no remaining actionable defect in the pool and completed App adapter. The [browser driver review](effects/driver-independent-review.md) records two initial false-pass gaps and their verified corrections: player-specific splash attribution, and exact authored scenery/presentation checks. Reviewers ran no additional game, GPU or image loop.

## Actual browser acceptance

The corrected journey passed all **eight stages in 8.630 seconds** on the frozen `web-r02` package at 1280 × 720. The outer process exited 0 with `status: passed`, `browserErrors: []` and `sample.errors: []`. The [portable runtime summary](effects/runtime-summary.json) records source hashes, exact invocation, artifact hashes and measured results. Raw evidence is the [journey](effects/browser/accepted-r02/summary.json), [outer browser report](effects/browser/accepted-r02/startup.json) and [runner log](effects/browser/accepted-r02/runner.log).

| Actual behavior | Measured result |
| --- | --- |
| Helm movement through water | 0.35563 m horizontal XZ travel; positive accepted drive; 1 wake and 9 foam sprites emitted; 10 sprites encoded |
| Pause across fresh frames | Effects tick 271, live count 11 and all emission counters unchanged; water tick unchanged |
| Robot water entry | 12 player-attributed splash sprites emitted; 17 total live sprites encoded |
| Rescue and durable acknowledgment | Pool and emission counters reset to zero; recovery saved before Resume became available |
| Boat and resource preservation | Exact original part/connection/settings blueprint; 11 parts, 1,035 kg, no paid IDs and stock 48 material / 0 machinery |

Every stage retained its world/build/root owner, coherent presented packet, later submission and actual GPU/physics completion proof. This run wrote **no image or video**. Its runner-reported retained browser profile was inside a temporary Nix shell directory that no longer existed after exit; the stored reports remain valid, but no reusable browser storage is claimed.

The [first attempt](effects/browser/failed-r01/summary.json) stopped after two recorded stages in 11.513 seconds. It reached the helm and emitted wake/foam, but its wait omitted the travel requirement and immediately asserted distance afterward. This was a premature driver assertion, not a demonstrated game defect. The corrected predicate waits for more than 0.35 m of **horizontal XZ** travel together with speed, drive, wake, foam and encoding; wave-driven vertical displacement cannot satisfy it. No product or package change was made. The original local pre-drive pose was not saved before the failed assertion, so that attempt's exact travel delta cannot be reconstructed.

The [failure's outer report](effects/browser/failed-r01/startup.json), [failed runner log](effects/browser/failed-r01/runner.log) and [correction provenance](effects/browser/travel-correction-r01.json) are preserved. Root's original first-run visual capture remains separate evidence; it was not repeated for the corrected journey. The final driver hash is in the runtime summary; the earlier component summary remains a historical source snapshot.

## Reproduction

Focused component commands from the repository root:

```sh
nix-shell --run 'bazel test //tests:cove_effects --test_output=errors'
nix-shell --run 'bazel test //tests:cove_effects_path --test_output=errors'
```

For the new regression alone, add `--test_filter=CoveEffects.SwimmingEndpointVelocityClampStillUsesAcceptedEntryDisplacement` to the first command. Coordinate the GPU lane before running the second command. These commands describe how to reproduce; they do not imply another completed run.

Run the existing browser smoke tool against the frozen package with a fresh report directory and profile:

```sh
env -u VOXY_SMOKE_VISUAL_CAPTURE -u VOXY_SMOKE_PROFILE -u VOXY_SMOKE_RESUME_WORLD \
VOXY_SMOKE_COVE_EFFECTS=<new-effects-report-directory> \
VOXY_SMOKE_REPORT=<new-outer-report.json> \
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_TIMEOUT_MS=300000 \
VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=720 \
node scripts/smoke_integrated_wasm.mjs <frozen-web-package> salvage-cove
```

Use the host's normal display/Chrome/GPU environment. The exact accepted invocation used `gaming-x11`, desktop Chrome and port 43874; its full environment is in the runtime summary. Use an unused port for another run. The effects mode automatically skips the generic startup image. An explicitly requested `VOXY_SMOKE_VISUAL_CAPTURE=<new-capture-directory>` adds a real PNG and requested 12-second canvas movie through the separate root-owned recording helper. The containing visual-finish record owns that capture's actual results; [source review](effects/recording-source-review-r01.md) alone does not establish them.

[`validate_cove_effects.mjs`](../../../../../scripts/validate_cove_effects.mjs) has a 210-second ceiling after startup. Its eight stages use real F9/B/E/W/P/Space/R controls: original scenery and exact starter ownership; dock walking and boarding; accepted helm travel with growing wake/foam; pause and fresh-frame freeze; robot water entry with its own splash count; Rescue reset with automatic durable acknowledgment; and exact original blueprint bytes after reopening. It preserves 11 parts, 1,035 kg, 48 material, paid IDs and world/build/root identity. It does not inject impact events, refit, repeat the old construction/sailing matrix or write images itself.

Recorded stages require matching camera geometry, presented and effects ticks, all 39 scenery proxies, 1,241,888 scenery bytes and appropriate submitted scenery draws. They retain a matched observation, a later same-owner submission and actual completion. Per-render counters are not claimed to be atomically tagged submission facts. Held inputs are released on failure, and the driver retains the failed state. The accepted run proves these conditions for the eight-stage journey; it does not establish unobserved impact dust, final visual approval or the joint suite result.
