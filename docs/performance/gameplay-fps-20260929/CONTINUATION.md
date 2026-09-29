# Continued FPS work

The initial investigation ran for more than the requested three hours.
Work resumed at approximately 22:17 UTC after the request to continue.
**There is still no verified FPS improvement.**

Frontier is the provisional priority while the mode question is pending.
Actual-Cove caching does not affect Frontier: the engine selects a separate
terrain specialization for that mode.

## Next hardware experiment

[run-frontier-shader-screen.ps1](run-frontier-shader-screen.ps1) compares the
accepted CPU build against itself, replacing only three guarded shader sources:
mesh shadow admission, deferred water shadows, and the Cove background load.
The relevant Frontier changes are the first two. All 24 paired shipping-format
Intel composition cases matched exactly in the earlier correctness experiment.
This is correctness evidence, not a performance result.

Run from **Windows PowerShell**:

```powershell
& '\\wsl.localhost\Ubuntu\tmp\voxys-fps-20260929\run-frontier.ps1' -Run
```

The launcher verifies 20 pinned inputs. It creates a unique Windows temporary
output folder and an isolated browser profile. It runs four fresh worlds in
baseline/candidate/candidate/baseline order, at 1280 × 720, with 600 warmup ticks
and 600 ticks for each idle, orbit and straight-walk scenario. Actual 16:00
lighting is enabled and paused through the production controls. Profiling and
work wrappers are disabled. The existing route, health, lighting, hardware and
frame-count checks remain required.

This session's workspace-write execution environment rejects Windows process
launches with `UtilBindVsockAnyPort: socket failed 1`. Windows PowerShell execution
and UNC source access have not been tested here. The launcher has been reviewed
offline and its input hashes verified. It does not change execution policy.
All owned compilation and software-GPU jobs have been stopped so they cannot
contend with a user-run capture. An actual clean capture remains required.

The resulting `report.json` is written in the output folder printed by the
launcher. A promising screen must be repeated in three balanced blocks and
checked for frame-time tail regressions before promoting the shader changes.

## Additional candidate work

The actual-Cove cache review found and repaired missing material invalidation.
Setters now also invalidate when an updated dynamic depth/shadow view aliases
the static input. Timestamp-start tracking reflects an actual attached query.
All changes remain isolated under `/tmp/voxys-fps-20260929/`.

The expanded native fixture captures whole BGRA8 color, R32 depth and RGBA16
opaque HDR targets. It prepares cached-versus-refreshed controls for texture
changes, rebakes, discarded frames, mode transitions, camera/resize changes and
underwater animation. Both native render/environment variants compiled with
matching reconstructed project flags and one documented baseline warning
exception. The updated baseline fixture compiled; compilation of the final
candidate fixture was interrupted. Native linking and GPU execution are
unfinished. Three required WASM translation
units compiled with the captured original O2 policy; final linking was interrupted
for the quiet timing window. No completed Cove browser package or lifecycle pass
is claimed.

A separate Frontier design splits geometry-cache invalidation from sunlight
invalidation. Current sunlight changes trigger full terrain traversal even with
a stationary camera. The proposed auxiliary stores exact raw LEGO hit metadata,
because the existing half-format material texture loses precision needed by
shadow decisions. It adds 14.06 MiB at 720p and extra moving-camera traffic.
Implementation, exact target-driver output and clean performance measurement
are all pending; this is a design, not an FPS gain.

An offline cache-work reporter now distinguishes terrain depth/shadow bakes
from background-color bakes. All seven offline tests now pass, including the new
Cove pipeline and mixed bake labels. It runs separately from clean timing and
makes no GPU-completion or FPS claim.

The offline push review also found a missing guard in the runner's in-process
ABBA comparison. It now rejects instrumented captures and diagnostic JSON
clocks as clean performance results. All 26 gameplay checks and seven cache
checks pass; the validation record is
[push-validation.json](evidence/continuation/push-validation.json).
No GPU, browser or hardware timing run was part of that review.
