# Affected physics suites — 2026-09-18

All six targets passed, 110 test cases, no failures. Imported assembly was reused
from its unchanged successful build; the other five ran sequentially.

Command: `nix-shell --run 'bazel test //tests:gpu_authored_shapes //tests:gpu_dynamic_solver //tests:gpu_islands //tests:imported_assembly //tests:imported_wall_physics //tests:imported_stack --local_test_jobs=1 --test_output=errors'`

| Suite | Cases | Skipped |
| --- | ---: | ---: |
| `imported_assembly` | 14 | 0 |
| `imported_wall_physics` | 8 | 0 |
| `imported_stack` | 7 | 0 |
| `gpu_authored_shapes` | 64 | 1 |
| `gpu_dynamic_solver` | 10 | 0 |
| `gpu_islands` | 7 | 0 |

The two opt-in actual-house scenarios also pass; see `manual-support-world.xml`.
UI build and all eight UI tests pass. The WASM CMake build passes after the native
HUD safeguards. These are affected-suite checks, not the full repository hook,
browser collapse acceptance, or a completed D2 gate. No commit/deployment claimed.

Latest preview: the updated WASM build loads in the in-app browser, restores the
isolated test world and mounts the Free Build UI without captured warning/error
logs. The local preview server was restarted after its earlier process exited.
This is startup/save-load evidence only: the new support-removal interaction has
not yet been accepted through the browser. The temporary click-to-look camera
preference was restored to its original hold-right-button setting.
