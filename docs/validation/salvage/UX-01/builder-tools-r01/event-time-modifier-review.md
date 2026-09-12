# Event-time modifier review

Read-only review, 2026-09-12. No actionable findings in the requested delta.

Reviewed `src/engine/platform/input.hpp`, the fresh-key paths in native and WASM
`input.cpp`, Application's workshop shortcut routing, and the fifth
`tests/test_gamepad.cpp` case. No builds, tests, apps, GPU work or screenshots
were run for this review. Root reports the five focused input cases and the
actual browser journey passing; those results were not rerun here.

Both implementations drain the ordered key queue and capture modifier state
only when a key transitions from up to down. Consequently a complete
Ctrl-down/Z-down/Z-up/Ctrl-up sequence retains Ctrl on the Z press even when
all held state is neutral at the rendered frame. A Shift press after R cannot
retroactively change that R event. Both left/right Ctrl, Shift and Alt are
included. Frame/reset/focus clearing removes stale modifier snapshots; repeats
do not replace fresh-press meaning.

Application queries these masks only with the corresponding fresh key edge.
Ctrl shortcuts suppress the same key's ordinary edit action; Ctrl+Y cannot
also paint, and modified R resolves replacement or its requested rotation
without also applying ordinary R rotation. Continuous camera controls still
use currently held Ctrl state, which is appropriate for a held action.

The fifth regression checks the fully released Ctrl+Z queue, non-retroactive
Shift-after-R, and reset clearing. Existing input handling coalesces repeated
fresh presses of one key within a displayed frame; this change preserves that
existing granularity rather than introducing an event-command queue. Broader
controller/menu routing was outside this final delta review and was reviewed
separately earlier.
