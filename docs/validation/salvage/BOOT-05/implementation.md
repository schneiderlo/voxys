# BOOT-05 — separate cove preview

Date: 2026-09-07. Executor: `render_architecture`.

This is a **technical placeholder** for an authored scene and lifecycle foundation,
not a playable salvage loop or representative visual progress. Flat primitive
fixtures are deliberately temporary; the concept image remains a separate target.
It has no GameSession, inventory, missions, salvage rewards, or vehicle building.
Visible browser and native-route evidence is tracked separately below.

## Implemented behavior

- `[game] mode = "salvage"` explicitly selects the new route. The shared resolver
  also accepts `terrain`, `ridgebreak`, `lego-shore`, and `lego-world`. Unknown or
  empty explicit modes fail. Absent mode preserves the previous exact title
  routes. Any declared WRECKWATER bootstrap conflicts with salvage.
- Native launch: `build-salvage-native/bin/voxy_native --config salvage.cfg`.
  Browser launch: `?experience=salvage`. The default URL remains LEGO WORLD.
- `salvage.cfg` uses the 256² shoreline and a 1,024-body allocation. This is a
  preview allocation, not a future game capacity/performance claim.
- Exactly **32 static authored fixtures**: 16 dock pieces, 12 wreck pieces,
  and four studded modules. Preview ownership excludes the character and every
  other world owner. Materials and shape data flow through the existing GPU
  primitive render path; four compound IDs feed the existing studded draw path.
- R or the browser Reset button queues a frame-boundary reset. Leave wins over
  Reset. Accepted removals are tracked individually; full command queues can
  retry without issuing an accepted removal twice. New spawns are transactional:
  failure cancels their pending lifetimes without clearing the whole world.
- A bounded one-slot metadata readback is encoded **after physics** only during
  Reset/Leave. The completed mapping must match the lifecycle revision and
  show each old generation dead or replaced before respawning. `encodedTick`
  alone is not completion evidence. This does not use the global debug snapshot
  or event channel. A stuck removal/map fails after ten seconds of frame time
  and closes the enclosing world; failed pre-submission initialization does not
  wait indefinitely for a GPU submission that never happened.
- Reset restores authored camera/character position and clears input. Leave
  restores the prior camera/controller, clears input, removes owned objects,
  then acknowledges to the browser UI, which navigates to LEGO WORLD.
- Preview rejects old playground/throwable actions and P/B/K/F7/F8 shortcuts.
  Escape still releases the mouse, then exits on native. Browser controls clean
  up their timer/listeners on Leave and page hide. They show no reward/economy.
- Diagnostic preview JSON exposes ownership/status/reset count plus camera
  sector/local pose, origin, controller, and mouse capture for lifecycle checks.

## Build identity

Fresh Release CMake native and Emscripten 6.0.1 builds passed. Native targets:
`voxy_native`, `voxy_tests`, `part_catalog_tests`. WASM target: `voxy_wasm`.
Each build used at most four workers. No browser or GPU was used by the builds.

The initial before/after source manifests match across **327 authored files**:

```text
05caca0cbddfe1c104b5621fc9611dea126911690da60b497e0a38567940d457
```

Base commit: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`, plus recorded uncommitted
work. `wasm-inputs-before.json` and `wasm-inputs-after.json` record the scope and
file hashes. The first browser journey exposed a real reset guard defect:
`PhysicsRenderView.residentBodyCapacity` shrinks with the live draw range and
cannot bound metadata reads of retired body generations. The corrected
Application predicate uses the actual GPU buffer size, rejects unrepresentable
`size_t` byte counts, and is exercised by the real GPU regression after the live
range shrinks. Initial failed evidence is preserved in `before-range-fix/`.

A second real browser reset exposed the zero-resident variant: the render view
intentionally returns no buffers after the last body is removed. Evidence is
preserved in `before-empty-world-fix/`. The final helper captures and retains
the metadata handle while fixtures exist, then validates against allocation
size even while the world is empty. The GPU backend allocates this buffer once
at initialization and never resizes/replaces it on update or reset. The preview
releases its reference (never destroys physics-owned storage) after readback on
Leave/shutdown, before the enclosing world destroys its allocation. Missing
metadata now fails preview initialization. Native uses `wgpuBufferReference`;
Emdawn uses `wgpuBufferAddRef`, verified against both installed headers.

The corrected native and WASM builds both passed, and the **46 CPU tests** and
fresh package gate passed again. The intermediate authored-tree fingerprint (330 files) was
`9402d4e5b002a62ee41139038a07d30c67d6ea0965131ab472d4a86d9e099ecc`;
see `range-fix-inputs.json`. This authored-tree scope also includes concurrent
unlinked build-model work, so use the executable/WASM hashes as the primary
runtime identity. `artifacts.json` records the corrected native and JS/WASM/data
bytes/hashes; previous artifact manifests are retained with failed evidence.
The final empty-world implementation is recorded in `empty-world-fix-inputs.json`.
A subsequent catalog-only compiler fix adds an explicit checked socket pointer;
its behavior and the preview path are unchanged. Both CMake artifacts are rebuilt
again after that integration fix and separately fingerprinted in `final-inputs.json`:
`18e22fdcfea77558faf7edf182ea726d348ada3f5bc80d7729d843c542bc522b`
(330 authored files). Native bytes remained identical after that catalog-only
change. The final WASM SHA-256 is
`d4b6575d97a84ba2c81a01c3c1710f9c9f551ed6f39cddda194bb808eed4644f`.
Known pre-existing GLM spaceship and mesh-header warnings are not suppressed.

```bash
nix-shell --run 'cmake --build build-salvage-native --target voxy_native voxy_tests part_catalog_tests --parallel 4'
nix-shell --run 'source /tmp/voxys-emsdk/emsdk_env.sh && emcmake cmake -S . -B build-lego-wasm -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DVOXY_BUILD_NATIVE=OFF -DVOXY_BUILD_WASM=ON -DVOXY_BUILD_TOOLS=OFF -DVOXY_BUILD_TESTS=OFF && cmake --build build-lego-wasm --target voxy_wasm --parallel 4'
```

## Completed focused checks

```bash
build-salvage-native/bin/voxy_tests --gtest_filter='Config*:SalvagePreview.*:ApplicationSalvagePreviewTest.*'
build-salvage-native/bin/part_catalog_tests
node scripts/test_salvage_preview.mjs
nix-shell --run 'WGPU_BACKEND=vulkan build-salvage-native/bin/voxy_tests --gtest_filter=SalvagePreviewGpu.CompletedMetadataAllowsBoundedResetAndPreservesUnownedBody'
make package-wasm WASM_BUILD_DIR=build-lego-wasm WASM_PACKAGE_DIR=/tmp/voxys-boot05-web
python3 docs/validation/salvage/BOOT-05/check_package.py /tmp/voxys-boot05-web
python3 docs/validation/salvage/BOOT-02/test_package.py /tmp/voxys-boot05-web
```

- **46/46 CPU tests:** mode fallback/override/rejection/roundtrip, pre-GPU
  incompatible-config rejection, bounded fixture ownership, failed spawn and
  respawn rollback, generation-safe reset, eight cycles without leaks, destroy
  retry, Leave priority, and owner teardown failure/retry.
- **16/16 catalog tests** pass in the same native build.
- **4/4 JS lifecycle tests:** duplicate clicks, stale Ready snapshots, Leave
  during Reset, acknowledgement before navigation, failure, and listener cleanup.
- **1/1 actual GPU test** passes on **AMD Radeon 890M Graphics, RADV STRIX1,
  Vulkan**. It performs three reset cycles, explicitly observes completed GPU
  metadata, checks an unrelated body's generation after Leave, then forces a
  real capacity failure and proves rollback preserves 101 unrelated lifetimes.
  It then removes every unrelated body and runs three additional resets with
  **zero resident bodies**, including a second completed empty-world frame before
  observing retirement, plus Leave. Both capture-before-empty and read-after-empty
  use the exact retained-source helper called by Application. Final clear and
  recapture verify releasing that reference does not destroy physics storage.
  `gpu-empty-world-test.log` records the final focused pass (199 ms).
  The final buffer-range regression is also included in the root mandatory suite; its final outcome is recorded separately.
- Fresh package: **58 preloads / 57,674,353 bytes**, five route configs,
  **17 local references**, **27 parity shaders**, and all five checked public
  KEEPALIVE bindings resolve to actual compiled WASM functions. Both new exports
  are present. All **six negative packaging controls** pass.

## Visible runtime validation

The retained-source build passed the browser Reset, R, Leave and fresh re-entry
checks. Its initial additional post-re-entry Reset attempt clicked during the
page loading sequence, before the parent panel had layout; no application action
was issued. That harness-only failure is preserved in
`before-journey-ui-readiness-fix/`. The click helper now waits for positive bounds
and an actual DOM hit test, then sends genuine CDP mouse input. It does not
change application completion or readiness state.

The reusable browser journey is `scripts/validate_salvage_preview.mjs`, called
by the shared smoke runner using `VOXY_SMOKE_SALVAGE=<evidence directory>`.
It uses real CDP mouse/keyboard input and observed application state. It covers
walking, Reset, R while W is held, pointer release, old-control isolation, Leave
acknowledgement, the old world after navigation, and fresh re-entry.

Final native salvage alone passed after the empty-world fix: exit 0, no
application/GPU errors, a visually inspected 1600×900 screenshot. See
`native-final-salvage/`; its exact native SHA-256 is
`c4dc00f1023fa50869dffcb2c76466a5c9a8c69964e7ccec34a6288b8b386d1d`.
The subsequent catalog pointer check does not execute in this preview.

The final visible hardware browser journey **passed** on AMD RDNA-3
(non-fallback adapter). `browser-salvage.json` and `browser-journey/summary.json`
record zero browser exceptions, zero console errors, zero observed GPU validation
errors, and no device loss. Actual pointer lock was granted and released.

| Observed stage | Preview fixtures | World residents | Reset count |
| --- | ---: | ---: | ---: |
| Initial cove | 32 | 32 | 0 |
| Reset button after walking | 32 | 32 | 1 |
| R while W was held | 32 | 32 | 2 |
| Old control/action isolation | 32 | 32 | 2 |
| Leave acknowledgement before navigation | 0 | 0 | 2 |
| Fresh cove re-entry | 32 | 32 | 0 |
| One real Reset after re-entry | 32 | 32 | 1 |

Both reset paths restored the original camera local coordinates exactly at
the diagnostic output precision: `[-12, 69.18, -24]`, sector `[0, -1, 0]`,
with original yaw/pitch and Character mode. Leave restored the saved FreeFly
camera, cleared input and hid the panel before navigating to initialized
LEGO WORLD. Six screenshots include the cove, movement/reset, old world,
re-entry, and separate study. They are behavior evidence, not visual quality
approval; the dock/wreck remain flat primitive placeholders.

The separate `lego_patch.html?test` page loaded and rendered. Its real buttons
switched 265 grouped bricks to 620 individual bricks and back, then dropped one
ball and reset to zero. This is an independent terrain study, not engine salvage
gameplay. The screenshot is `browser-journey/06-standalone-study.png`.

The final packaged **default, lego-world, lego, terrain, and ridgebreak browser
launches all passed**, each in a fresh visible browser with no browser exceptions
or console errors. `browser-legacy/summary.json` lists exact commands and per-route
reports/screenshots. Default and lego-world retain the existing 8192² landscape;
lego retains its 256² shore. The default landscape screenshot was visually
inspected and retains the existing studded shore/water scene.

```bash
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_SALVAGE=docs/validation/salvage/BOOT-05/browser-journey VOXY_SMOKE_REPORT=docs/validation/salvage/BOOT-05/browser-salvage.json VOXY_SMOKE_SCREENSHOT=docs/validation/salvage/BOOT-05/browser-salvage.png node scripts/smoke_integrated_wasm.mjs /tmp/voxys-boot05-web salvage
# Existing browser routes were then run individually with the same package and
# VOXY_SMOKE_GPU=gaming, per-route report and screenshot output paths:
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-boot05-web default
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-boot05-web lego-world
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-boot05-web lego
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-boot05-web terrain
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-boot05-web ridgebreak
```

Earlier native five-route captures and the WRECKWATER graphical regression
remain attributed to native hash `75f801bb…` in `native-route-report.md`. The
later application source delta only changes the salvage metadata reference
lifetime; existing routes were not relabeled as the final binary. The final
native salvage-only capture on `c4dc00f1…` verifies the affected route again.
WRECKWATER has graphical launch/log evidence but **no OS screenshot** because
the Wayland capture interface was unavailable. Root owns the final mandatory
Bazel suite and records its outcome separately. No FPS or frame-time acceptance
is claimed here; screenshot HUD counters are incidental.

## Final evidence index

- `build-empty-world-fix.log`, `build-catalog-check-fix.log`: completed native
  and WASM Release builds, at most four workers.
- `cpu-tests-empty-world-fix.log`, `gpu-empty-world-test.log`,
  `catalog-tests-final.log`, `ui-lifecycle-tests.log`: focused final passes.
- `package-stage-final.log`, `package-check-final.log`,
  `package-negative-final.log`: staged package and six negative controls.
- `package-http-final.log`: a temporary loopback HTTP server served the final
  package; the checker fetched and compared every declared web asset, parity
  shader and JS/WASM/data artifact byte-for-byte. Server stopped normally.
- `artifacts.json`, `final-inputs.json`: executable/package hashes and authored
  source fingerprint. Native cove and final browser use these exact artifacts.
- `before-range-fix/`, `before-empty-world-fix/`,
  `before-journey-ui-readiness-fix/`: preserved failed runs with their cause
  distinguished above. No failed run is reported as a pass.

