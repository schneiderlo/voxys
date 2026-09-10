# Section-aware cove saves and restoration

Implementation continues under D32. The live host now captures SVCE v4 and
accepts v1–v4 at startup. Three-section CPU reconstruction and native/browser ordinary
save/restart are verified. Full live cut
publication, fragment recovery and gate acceptance remain open. No screenshots
or gate commit.

## Implemented behavior

- `CoveRestoreCandidate` reconstructs every compiled physical section from the
  accepted weld graph and the complete saved root table. It checks each motion
  against that section's mass frame. It creates no GPU handles or completed
  observations. The compatibility `boatMotion` mirrors the helm's root.
- Root-aware scene preparation excludes disabled welds only from the derived
  scene connections. Accepted disabled bonds and all paid/loan parts survive.
  Ordinary Launch retains its connected-design requirement. Removed starter
  placements do not reappear as static player collision during loading.
- The player binds collision parts and rigid poses to durable root keys.
  An aboard player follows the saved rider root. Detached parts no longer
  support the player through their old blueprint positions on another root.
  Helm mode requires the helm section; rejected keys/poses preserve the player.
- The application owns and uploads every root, admits the full restored set
  before the neutral restore tick, and waits for a common observation tick.
  Zero-buoyancy roots receive no water driver. Normal Launch and initial
  admission establish the same player bindings.
- Tow anchors, body bindings, rope drawing, hook/reel controls and observations
  use the section carrying the accepted winch. The rider must be aboard that
  section to operate it. Existing multi-root harbor suspension is refused.
- Capture writes every root's key/motion plus controlling helm and rider IDs.
  Read-only diagnostics expose these IDs, poses and velocities so verification
  compares actual completed runtime state against the archive.

## Executed verification

| Check | Result and scope |
|---|---|
| Bazel CPU cove selection | 39 cases pass; excludes actual GPU fixtures. |
| CMake restore selection | 3 cases pass: legacy restore, detached rider, three-section restore. |
| Application builds | Bazel native, CMake native and configured WASM pass. |
| Native purchases/save/restart | 14 actual-control stages pass. Buy a pontoon, save v4, restart, buy another, save and restart again. 12/13 parts, 1155/1275 kg, 24/0 material and exact paid IDs survive. Competing/missing selected worlds refuse. |
| Native legacy upgrade | 3 stages pass from a copy of the historical v1 delivery checkpoint. Actual load → F10 v4 save → process restart preserves paid part 35, 96 material, completed job, onboard root 7 and wave phase. The historical source bytes remain unchanged. |
| Browser first run | Purchases and two actual v4 save/reloads pass. Later walking fails; this run is not a complete journey pass. |
| Browser continuation | All 9 stages pass in `browser-saves-r05`. The same saved paid boat boards, hooks/reels, saves/reloads with attached cargo, reels/releases again and sails at 2.07272 m/s. Leave drains successfully. |

The new three-section fixture cuts the lowest-ID pontoon and the winch out of
the actual 1035 kg authored boat. The helm remains on root 1. Distinct translated,
rotated and moving saved sections restore independently; the rider is on root 0
and the winch anchor uses its own root's frame. Every part, disabled bond and
motion survives, with no body/shape handles, invented ticks or adapter calls.
This is a CPU preparation test, not a live GPU cut or runtime fragment journey.

`checks/player-cpu-r04.xml` and `checks/cmake-restore-r01.xml` contain exact
case results. Native control states, source archives and process logs are in
`native-saves-r01/` and `native-upgrade-r01/`. The native binary is frozen at
`build-cove-root-resume-_csjbh9y/native-resume-r01`; the browser package is frozen
at `build-cove-root-resume-_csjbh9y/web-r01`. Hash manifests identify exact code,
scripts and artifacts. These checks make no displayed-frame performance claim.

The final browser uses Chrome 152.0.7977.82 and the `gaming-x11` hardware
profile; `browserErrors` is empty. The actual towing archive is 15,778 bytes,
schema 4, with root/rider 7 and controlling helm part 15. Its 7,917-byte logical
checkpoint and 7,236-byte retired parent both survive validation. Save tick
134 restores at neutral tick 135 while wave time stays 2.15 seconds and cable
length stays 6.41544 m. Reeling after load shortens it to 5.98211 m. Final
sailing spans 120 further completed physics ticks. The earlier failed full
runner and the successful resumed continuation are kept as separate reports;
they are not represented as one uninterrupted passing journey.

`checks/artifacts-r03.sha256.json` identifies the final 42 source/script/frozen
artifact inputs, including the corrected browser control driver. Earlier
manifests preserve the preceding driver versions. Verification scripts use
only actual controls, read-only observations and separate temporary storage;
the optional input trace observes trusted pointer events and changes no game state.

## Retained failures and corrections

- `root-resume-cpu-r02` failed to compile because the new test implicitly
  converted size indices to doubles. Explicit conversions preserve strict warnings.
- `root-resume-cpu-r03` rejected the three-section fixture: its origin was
  −200 m but its water datum was still zero. The fixture now supplies its actual
  matching datum; production validation remains unchanged. All 39 CPU cases pass.
- Browser r01 completed both paid saves and reloads, then the old 14 cm waypoint
  driver failed to reach the boat. The save runner now uses the established
  25 cm steering waypoint from the delivered cove journey and still requires
  actual Board/Helm interaction availability. Physical reel/sailing holds count
  completed physics ticks, independently from player/displayed-frame ticks.
- Browser r02 failed before navigation: the retained profile's stale debugging
  port was read before the new Chrome endpoint was ready. The startup runner
  now waits for the endpoint announced by its own child process. No leftover
  browser using this isolated profile was found afterward.
- Browser r03 reached boarding, Helm, hooking and reeling, then waited for a
  Pause click that never reached the button. R04 adds a bounded observational
  pointer/click trace and proves all three events hit the panel background
  after scrolling moved the button. The runner now waits for a stable visible
  hit target after scrolling/hovering and verifies actual trusted click delivery.
  No game action, pause rule, acceptance threshold or save state was bypassed.

## Reproduction

Run from the repository root inside the Nix toolchain. Use new output/save
directories; keep GPU journeys sequential and preserve prior evidence.

```sh
bazel test -c opt --jobs=8 //tests:cove_player --test_filter='CoveMovement.*-CoveMovement.Actual*:CoveMovement.WaterPhase*:CoveMovement.HarborLiftActually*' --test_output=errors
bazel build -c opt --jobs=8 //:voxy_native
cmake --build build-native-save-host --target voxy_native cove_player_tests -j8
build-native-save-host/bin/cove_player_tests --gtest_filter='CoveMovement.DetachedRider*:CoveMovement.FragmentArchive*:CoveMovement.OwnedRestore*'
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8
python3 scripts/validate_native_cove_saves.py --binary <frozen-native> --storage-root <new-root> --output <new-evidence>
python3 scripts/validate_native_cove_upgrade.py --binary <frozen-native> --source-slot <existing-v1-v3-slot> --storage-root <new-root> --output <new-evidence>
```

For browser playback use Node 22+, `/usr/bin/google-chrome`,
`VOXY_SMOKE_GPU=gaming-x11`, `VOXY_SMOKE_NO_SCREENSHOT=1`,
`VOXY_SMOKE_KEEP_PROFILE=1`, `VOXY_SMOKE_TIMEOUT_MS=1800000`, and new
`VOXY_SMOKE_COVE_SAVES` / `VOXY_SMOKE_REPORT` paths. Run
`scripts/smoke_integrated_wasm.mjs <frozen-web> salvage-cove`.
To continue this retained two-pontoon save, select the marked isolated profile
`/tmp/voxys-startup-jLGqhu`, port 41289 and world
`00a297a0ef96a7134f245d2d48080acf` through `VOXY_SMOKE_PROFILE`,
`VOXY_SMOKE_PORT` and `VOXY_SMOKE_RESUME_WORLD`. Do not change the owner's
separate preview on port 38206.

## Required next work

1. Test actual multi-root runtime admission and save/reload, including detached
   winch/rider cases, allocation/water-driver capacity and partial rejection.
   Startup currently fail-stops on admission failure; that is not proven rollback.
2. Finish atomic cut/render publication, reserving all children and joining
   their execution with parent retirement before publishing ownership.
3. Protect the last intact design and generalize Rescue/rebuild to every remote
   section and each paid/loan part exactly once. Current Rescue/reset still
   moves only the primary root and must not be used to claim fragment recovery.
   Prepare bounded teleport/zero-velocity commands for all sections and eligible
   cargo before enqueueing any of them. Use the existing joined-boundary
   `PhysicsMutationBatch::bodyCommands` reservation/commit after rope retirement;
   do not extend the current unchecked per-body `enqueue` loop. Confirm every
   moved root at the same completed tick before acknowledging the recovery save.
   Starter rebuild currently calls design normalization on the cut build and
   refuses disabled bonds; preserve the pre-cut design before this service is
   enabled for fragments rather than silently clearing that refusal.
4. Complete cross-section locomotion/inherited jump velocity under ACT, and
   root-aware harbor attachment policy. Expose cutting only after the required
   native/browser cut, reload, recovery and refusal journeys pass.
