# Browser pacing assessment

Read-only investigation of the existing slow-browser evidence and local scheduling code. **No scheduler change is justified by this evidence.** Root elected not to perform further browser/RAF diagnostics during the timed session.

## Recorded evidence

- `build-cove-mechanisms-r01/browser-startup-r01.json` used headed Chrome 152.0.7977.82 in `gaming-x11` mode: `--ozone-platform=x11 --enable-features=Vulkan`. It used the normal `?experience=salvage-cove` URL. The startup sample already showed frame 33 at about 1 FPS, 1001.10 ms current interval, 1.60 ms CPU, one outstanding GPU frame of a twelve-frame limit, and zero pacing skips.
- `browser-journey-r01/timing-probe.json` later recorded frame 221 at 0.99935 FPS, 1001.50 ms current interval, 1.10 ms CPU, GPU execution 3.758532 ms, queue 1/12 and zero pacing skips. Its GPU timing sample was frame 210, eleven frames old; it is not a simultaneous wall-time throughput certification. The adapter was AMD RDNA 3 and not fallback.
- `browser-journey-r01/approach-probe.json` showed an active, ready, nonfailed Cove, running pause state, valid boat/physics ownership, no physics failure, and no pending job/refit. The failed journey's fourteen partial stages remain partial evidence; the journey was stopped after 237.308 seconds and is not a passed motion/save result.
- `browser-startup-r03.json` again recorded approximately 1 FPS before uncapping. Its log records the physical F9 toggle at 33.568 seconds and the switch from RAF to SETIMMEDIATE at 34.568 seconds, consistent with waiting for the next slow RAF callback. This report is not represented as a passed full journey.

## Focus and environment limitation

The timing probe reports `visibility: visible` and `focus: true`, but `scripts/smoke_integrated_wasm.mjs:278` explicitly enables `Emulation.setFocusEmulationEnabled`. Therefore `document.hasFocus()` is **emulated evidence, not proof of actual window-manager focus or unoccluded display presentation**. `Page.bringToFront` selects the page; it does not by itself establish the desktop compositor's refresh behavior. The harness also changes Chrome presentation/GPU/background behavior through its flags.

The run was X11, not a directly measured ordinary user browser session. No exact Chrome/X11/compositor cause was established. The data is consistent with browser/compositor delivery of RAF callbacks being restricted, but does not prove that normal visible player sessions run at 1 FPS.

## Local scheduling code

`salvage_cove.cfg:16` intentionally sets `vsync = true`. `src/engine/platform/wasm/entry.cpp:1045` applies scene pacing, with uncapped overrides only for explicit benchmark/throughput routes. Application initialization maps VSync to its uncapped flag.

The ordinary main loop registers `EM_TIMING_RAF, 1` at `entry.cpp:1371–1376`. The installed Emscripten source `/home/modkin/emsdk/upstream/emscripten/src/lib/libeventloop.js:340` schedules `requestAnimationFrame`; its frame-skipping branch at line 474 applies only when the timing value is greater than one. Thus value one means **every animation frame**, not one frame per second. There is no local one-second timer or requested 1 FPS limit in this route.

`entry.cpp:1316` skips processing only at the twelve-frame GPU queue limit; the recorded queue depth and zero skip count do not implicate that guard. Each admitted callback calls `processFrame`. The measured CPU/GPU work is far below the roughly one-second callback interval. F9 changes to the existing SETIMMEDIATE route; the unreachable Cove/workshop F9 binding was a separate proven bug and has already been corrected.

## Decision and any future verification

Preserve the normal default and the repaired physical F9 control. Do not claim the test's emulated focus proves a normally presented game is slow. Do not default all play to uncapped or add a timer watchdog on this evidence.

If selected in a later task, the smallest discriminating verification is a short ordinary RAF callback count beside game frame telemetry in a genuinely visible/focused user tab without focus emulation, compared with an empty RAF counter in the test window. If both counters are slow only in the harness window, investigate test/window presentation. If bare RAF is healthy while game frames stall, investigate the game path. That distinction needs no rebuild, screenshot, machinery journey or speculative default change.

No browser, GPU, build, test, screenshot, image or new performance experiment was run by this reviewer. Only existing evidence and local source were read.
