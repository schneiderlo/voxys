# Hold Shift to run

Creative/free-build play accepts either Shift with WASD for 1.75x walking
pace. Release Shift to walk. The brick palette can stay open; Shift remains
available to its fine-placement logic. The bike retains its own controls.

The creative locomotion sample ignores the Shift chord modifier while the
workshop receives the original sample. This matters because exact unmodified
WASD bindings previously rejected Shift+WASD entirely. Pace uses the existing
bounded player speed multiplier and collision/step solver. Running jumps and
air steering retain horizontal pace; jump height is unchanged. Swimming is
unchanged. Existing animation cadence follows accepted movement speed.

Native world, runtime and combat targets pass. Tests drive actual LeftShift,
RightShift and D input through the creative runtime, verify 1.75x displacement
in walking/build modes and restoration of walking after release. A separate
controller regression verifies running-jump horizontal speed and a thin wall
at full pace. Browser build succeeds; no new image capture is needed for this
input behavior. The menu help and README document Shift.

## Running-jump correction (2026-09-17)

The original input adaptation removed Shift from held-key modifiers but left
the modifiers captured on each key press unchanged. Consequently, Shift+Space
never produced the Jump press edge. The movement sample now removes Shift from
both; the original workshop sample and Control/Alt modifiers remain intact.

The new movement regressions failed before the correction and pass afterward.
They cover press edges, quick taps, held-key repeat prevention, menu rearming,
building mode and preservation of other modifiers. The full-terrain native
runtime test drives LeftShift/RightShift + D + Space in explore/build modes and
checks upward displacement and running horizontal pace. Both runtime tests,
all 35 world tests, all four movement tests and the input-preferences suite pass.
The browser game rebuild (`cmake --build build-wasm --target voxy_wasm -j 4`)
also succeeds. Browser UI interaction was not separately exercised.

Validation corrected an existing undefined compiler-options name in the cannon
test target to use `PROJECT_TEST_COPTS`. Existing cannon-related sign-conversion
and double-promotion warnings required per-file `-Wno-error` exceptions for
`cannon_physics_scene.cpp` and `adventure_runtime.cpp` during native validation.
Those unrelated source files were not changed by this fix.
