# Browser builder acceptance

**Passed: nine stages in 41.574 seconds**, using the frozen `web-r02` package.
The outer runner exited 0 and reported passed. Browser, sample and console error
arrays were empty. The temporary Linux gamepad exited 0 with no stderr.

This is automated input through the real Linux controller and browser backend.
It is not a human playtest. No screenshot, game-state setter or replacement
`navigator.getGamepads` implementation was used.

## Actual player journey

1. Controller View opened the workshop. R3 + right stick panned; L3 + right stick
   zoomed. Camera controls preserved design and stock.
2. Remove/Keep cleared the cradle. A placed one 2×2 brick; B discarded the next
   unused ghost. Copy produced a readable overlap refusal, then Cancel restored
   the exact original blueprint.
3. Copy/Snap/Keep added a second brick. Both were selected as a group. Raising
   them produced a disconnected preview; Keep refused without changing owned
   parts, stock or revision. Cancel restored the exact design.
4. Undo and Redo removed and restored the copied brick exactly.
5. The controller on-screen keyboard named BUILD and COPY. Save, Copy and Update
   durably stored both designs. Group paint changed COPY to Blue.
6. Launch bought exactly two bricks. A physical Save button click saved the
   paused expedition. One real page reload restored its exact owned boat.
7. Controller Load read BUILD from durable storage; exact exported blueprint
   bytes matched. Launch restored its original appearance at zero extra cost.

Final boat: **12 parts, 993 kg, owned purchase IDs 35 and 36, 42 material and
0 machinery**. Names BUILD and COPY persisted. Blue boat/design bytes were
verified after reload; the final free Launch used BUILD's original paint.

The driver records nine states and later same-owner fixture submissions, then
requires actual GPU and physics completion. It checks the existing 16 MiB fixture
limit and exactly nine presentations. Each OS button's standard browser index is
read and checked while held; this is observation, not an input override.

## Evidence

- [Exact states, controls and blueprint bytes](accepted-r04/summary.json)
- [Compact outer-run metadata](accepted-r04/outer-metadata.json)
- [Exact frozen package hashes](accepted-r04/package-hashes.json)
- [Final driver, helper and UI source hashes](accepted-r04/source-hashes.sha256)
- [Raw runner log, losslessly compressed](checks/browser-builder-r04.log.gz)

The compact outer metadata records the hash of its original full report. The
raw compressed runner log retains that report without copying its large JSON
again. No binary game or art payload is duplicated here.

Focused Node checks also passed: controller menu 21 cases, design-library UI
11 cases and all 36 printed salvage UI groups. Their exact logs are in `checks/`.
These DOM-model checks verify permissions, focus and async ownership; actual
controller execution comes from the journey above.

## Preserved failures and corrections

| Attempt | Actual result | Correction |
| --- | --- | --- |
| Setup | Nix daemon socket denied; no Chrome run | Authorized host execution |
| r01 | 22.342 s; View/RB worked, Menu handoff failed; zero captured stages | Application public bridge names and event keys quoted against Closure renaming |
| r02 | 27.042 s; two captured stages, then supposed Snap rotated the brick | Test controller X/Y evdev labels corrected to 307/308; app unchanged |
| r03 | 1.828 s; camera comparison failed before any draft edit | Driver waits for visible workshop and 200 ms stable initial framing before measuring gestures |
| r04 | Nine stages passed, 41.574 s | No further correction |

Exact failure states and outer metadata are in `failures/`; source-supported
reviews and raw failure logs are in `checks/`. The camera failure was a baseline
timing error, not a camera behavior fix. The X/Y error belonged to the temporary
test device, not the game Snap command.

## Reproduction and final-package boundary

Run hosts sequentially: Chrome and native share the OS controller/focus lane.
Use a new evidence directory and the exact package hash inventory. From the
repository's Nix environment, the successful runner was:

```sh
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.8NQFV3 \
VOXY_TEST_CHROME=/opt/google/chrome/chrome \
VOXY_SMOKE_GPU=gaming-x11 \
VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_KEEP_PROFILE=1 \
VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 \
VOXY_SMOKE_TIMEOUT_MS=420000 \
VOXY_SMOKE_COVE_BUILDER_TOOLS=build-workshop-tools-r01/browser-builder-r04 \
VOXY_SMOKE_REPORT=build-workshop-tools-r01/browser-startup-r04.json \
/home/modkin/.nix-profile/bin/node scripts/smoke_integrated_wasm.mjs \
  build-workshop-tools-r01/web-r02 salvage-cove
```

The driver itself has a 300-second bound. Use fresh report names when repeating.
The X11 authorization path is host/session-specific.

**Retained-profile limit:** the runner reported its isolated profile beneath
Nix's temporary directory. `KEEP_PROFILE` stopped the runner deleting it, but
Nix removed the enclosing directory after process exit. That profile no longer
exists. The real in-process page reload above passed; a later exact-world
continuation from this profile cannot be claimed or reconstructed.

The final `web-r03` package adds only the independently reviewed
`session_recovery.cpp` future-Undo currency-admission fix to this tested product
source. Its focused shared recovery checks and smallest final browser startup
belong to the parent checkpoint; this document does not claim those results,
the whole required suite or publication. The nine-stage builder journey will not
be repeated for that unrelated guarded recovery condition.
