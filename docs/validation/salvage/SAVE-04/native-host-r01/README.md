# Linux expedition save and process restart

Completed component of SAVE-02/04. The full tasks and G04 remain open.
Baseline HEAD: `7f28fabfabf3d63726f6cfa1001c3ce1ed557911`, with the authorized
uncommitted implementation tree. `manifest.json` records exact source, runtime
and evidence hashes. No screenshots, image review or performance claims.

## What players can do

Launch `--config salvage_cove.cfg`. Close the workshop, press P, wait for
Paused, then F10. The title reports Saving and then Expedition saved only
after both disk replicas finish publication. These are manual checkpoints.
The startup log prints the world ID, folder and resume command.

Use `--expedition-world <32 lowercase nonzero hex digits>` to reopen a save.
`--expedition-root <absolute-folder>` optionally overrides the default Linux
XDG/HOME save root. Keep the same root when reopening. Missing selected saves
and competing owners fail before game initialization. Loaded expeditions
start paused; P resumes neutral controls. There is no save-selection screen,
automatic save, native named-design library or Windows storage implementation.

## Implementation and ownership

- `NativeSaveWorker` owns one `NativeSaveStore` on a dedicated thread. It
  accepts at most one open/publish operation, including an unconsumed result.
  Owned immutable bytes cross the boundary. The main loop only polls results.
  Shutdown drains an accepted write, releases the file lock and joins; callers
  serialize shutdown. A fault observer must outlive the worker.
- `NativeCoveSaves` owns the host lifecycle and calls `Application` only on
  the main thread. Initial disk reading may wait before `Application::init`.
  Selected-world recovery stages the saved source, publishes the fresh-owner
  and retired-parent SVCE pair, then acknowledges its exact whole-archive
  digest. Physical activation cannot precede this durable publication.
- F10 captures through action 1 at the existing certified Pause boundary.
  A new world requires an empty slot. Later saves compare the owned generation.
  A failure before publication remains retryable. Uncertain publication,
  conflict or loading failure closes admission/freezes the expedition; it
  requires restart and reconciliation. Closing does not report a late success.
- The existing shared restore admits boat, cargo and player after exactly one
  neutral physical tick. It retains paid identities, inventory, content and
  water state. This is semantic resume, with a small settling step.
- `gpu::Context::tick()` now performs nonblocking wgpu-native polling. The
  prior loop could stop retiring GPU callbacks once submission backpressure
  stopped new work. A real GPU map/queue-completion test reproduces the required
  callback progress without more submissions. Dawn keeps its event processing;
  browser callback handling is unchanged. Native title updates are guarded so
  the shared application still builds for WASM.

## Actual checks

All commands ran in the repository's Nix shell. Native gameplay used the real
WebGPU renderer, 960×540 X11 window and the normal cove configuration. Window
selection used the child process's `_NET_WM_PID`; keyboard events targeted
only that window. No focus switching or game-state setters were used.
Save folders were isolated under `build-native-save-validation-r05/saves` on
workspace ext4 (`/dev/nvme0n1p6`). No user saves were modified.

| Check | Result |
| --- | --- |
| Bazel optimized native application and combined test build | Passed |
| Affected native GPU/context/cove/session/format/store/worker suite | 335 passed, no skips |
| Focused format/store/worker tests on ext4 | 16 passed, no skips |
| Clean CMake native application build | Passed |
| CMake format/store/worker tests on ext4 | 16 passed, no skips |
| CMake WASM application compatibility build | Passed |
| Final actual native journey r05 | 14 records, 3 game processes, 2 saved-world restarts, passed |

Final journey world: `d08fceb6fb091d4f71e77ab39a08a15e`.

| Step | Physical boat | Material | Save tick / epoch |
| --- | --- | --- | --- |
| Buy, Keep, Launch, Pause, Save | 12 parts, 1,155 kg; paid ID 35 | 24 | 33 / 1 |
| Terminate process, reopen | Same parts, mass and paid ID | 24 | 34 / 2 |
| Resume, buy again, Launch, Save | 13 parts, 1,275 kg; paid IDs 35 and 100 | 0 | 69 / 2 |
| Terminate process, reopen again | Same 13 parts, mass and paid IDs | 0 | 70 / 3 |

The file wrapper and inner SVCE digest were independently checked from actual
`current`/`mirror` bytes. Copies agreed after acknowledgment. Manual saves
published generations 1 and 3; recovery published the intervening fresh-owner
generation and the final generation 4. The world, inventory, paid identities
and water time survived each restart; LEGO terrain remained selected.
A competing game process was refused while the first owner lived, with the
save bytes unchanged. A missing selected world was refused before initialization.

The driver sends SIGTERM only after a committed checkpoint, then starts a new
process. This is a real process restart test, not power-loss certification or
an interruption during a live write. The worker suite separately covers a
delayed accepted write draining during close, ENOSPC before publication and
post-rename directory-flush uncertainty. Existing store tests include their
16 actual SIGKILL boundaries; injected faults remain labeled as injected.

## Reproduction

Use fresh output and save directories. Do not reuse the example paths if they
already exist; the driver deliberately refuses to overwrite them.

```sh
nix-shell
bazel build -c opt //:voxy_native //tests:voxy_tests
bazel-bin/tests/voxy_tests --gtest_filter='ContextTest.*:GPUTimerTest.*:CoveSave.*:*Cove*:*FixtureRegistry*:*Session*:SaveGeneration.*:GpuAuthoredShapes.*:NativeSaveStore.*:NativeSaveWorker.*'
python3 scripts/validate_native_cove_saves.py \
  --storage-root /absolute/fresh/save-directory \
  --output /absolute/fresh/evidence-directory
cmake -S . -B build-native-save-host -DCMAKE_BUILD_TYPE=Release -DVOXY_BUILD_TESTS=ON -DVOXY_BUILD_BENCHMARKS=OFF
cmake --build build-native-save-host --target voxy_native native_save_store_tests -j8
VOXY_STORE_TEST_ROOT="$PWD/build-native-save-host" build-native-save-host/bin/native_save_store_tests
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8
```

The final CMake target is `native_save_store_tests`. The earlier command used
`voxy_test_native_save_store`: it failed after successfully building the native
application. The corrected target built and all 16 tests passed. Logs preserve
both results. The WASM command requires the existing configured EMSDK tree;
use the repository setup instructions when preparing another machine.

## Retained failures and limits

- r01 exposed the native GPU callback stall; it failed before saving. Fixed by
  nonblocking Context polling, then covered by the dedicated callback test.
- r02/r03 exposed duplicate XIM forwarding of the driver's zero-timestamp key
  events. That toggled the workshop twice. Nonzero monotonic event timestamps
  allow GLFW's existing duplicate suppression to work. r03 retains the actual
  key trace. Temporary trace logging was removed from shipping code.
- r04 passed both restarts with tracing enabled. r05 uses final normal logging
  and adds competing-process/missing-save refusal.
- Earlier native builds failed on X11's `None` macro and strict indentation
  warnings; both were fixed. The first WASM build failed on a native-only
  window-title method; its platform guard fixes that build.
- The actual restart journey tests paid construction. Native towing/banking
  restarts, live storage faults in the game, Windows, latching, fixed-tick
  WaterField, save selection/export/import, autosave and durable delivery
  receipts remain open. The existing browser towing/reload journey is historical
  evidence at `../resume-r01/`; it was not rerun for native-only host changes.
- Independent review, broader game gates and release/human acceptance are not
  claimed. No full gate passed and no gate commit was made.

## Next implementation contract

1. Connect actual cargo delivery to durable completion in both hosts. The
   current in-memory H/Deliver reward is not a durable receipt. Secure the
   physical cargo, finish the canonical transaction, join an exact paused
   physical/logical archive, publish it through the owning store, then show
   delivery success. Define failure/restart behavior so accepted cargo/reward
   cannot duplicate or disappear. Never treat the RAM journal's written-prefix
   test helper as a disk acknowledgment.
2. Prove the real hook/tow/return/deliver path with shipped controls, then
   restart and reject a second reward. Include failures before/after physical
   securing, logical publication and disk completion. Update player text to
   distinguish securing/saving, success and recovery-required states.
3. Add useful world selection/export/import and remaining native/browser live
   failure cases. Windows needs its own real file backend and platform proof;
   do not mark Linux evidence as Windows support. Preserve all prior routes,
   stepped terrain/studs and the existing browser preview origin.
4. Mark only completed scoped items. SAVE-01/02/03/04 and G04 stay unchecked
   until their complete criteria pass. On a full gate, run the required
   repository checks and commit that gate's completed work as instructed.
