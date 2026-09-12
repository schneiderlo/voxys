# Native Cove HUD component

Implemented 2026-09-12, within the owner's 03:27:33–07:27:33 UTC work window.
The native gameplay integration and independent source review now pass. Root
owns final combined publication. No screenshots or rendered image files were
created.

## What is implemented

- Native Cove-only panel after all scene composition. Workshop title, selected
  part/brick, material stock and kept-design Launch debit, current placement
  validity and three short lines of keyboard hints. Outside the workshop, nearby
  interaction and pause/save status replace building information.
- No action IDs, save data or canonical game state added by HUD. The builder
  agent's continuous brush provides `brickToolActive()` and keyboard behaviors.
- Mixed-case DejaVu Sans with at least 20 px body type on supported desktop
  windows. A dark backing, spacing, word wrapping and bounded truncation handle
  smaller windows. This is initial native UI, not complete controller or native
  named-design UI.
- The native workshop camera reserves the same left sidebar and bottom palette
  space. The builder agent owns that camera/picking hookup and uses actual
  swapchain dimensions for high-DPI consistency. Palette clicking remains live.
- One alpha-blended draw, one R8 512 x 256 atlas and one 768-instance vertex
  buffer: **167,936 bytes / 164 KiB** of fixed texture/buffer data. This figure
  excludes driver/pipeline/bind-group overhead. No depth target or world data.
- Read-only model sampling at 10 Hz. Identical content and viewport reuse the
  existing GPU vertex contents. Command buffers retain released resource owners
  through actual submitted completion; shutdown uses Release, not Destroy.
- Top-level `nativeHud` observation: `enabled`, `lastEncodedQuads`, `uploads`,
  `bodyPixels`, `truncated`, `panel: [x,y,width,height]`. The quad count is last
  encoding evidence, **not proof of GPU completion**. Root's actual application
  journey should observe it while the normal completed-frame counters advance.

## Font provenance and build integration

- Unmodified DejaVu Sans 2.37 at `data/fonts/cove-hud/DejaVuSans.ttf`.
- `LICENSE.debian` contains exact Bitstream Vera notices and permissions;
  `README.md` distinguishes the unrelated Debian packaging license paragraph.
- Recipe `python3 tools/make_cove_hud_font.py --check` passes. Pillow **12.1.1**.
- Generated `src/render/cove_hud_font.inc` records font and decoded-atlas hashes.
  Runtime needs no system font installation. Shader: `shaders/cove_hud.wgsl`.
- New renderer and test have Bazel and CMake entries. Font notice accompanies
  Bazel native runfiles and CMake installs; native CMake's existing data copy
  also includes the source/notice.
- If freezing only the native executable into a new package, also retain
  `data/fonts/cove-hud/LICENSE.debian` with the package, since the executable
  contains font-derived atlas data. CMake install already does this.

## Verification

`nix-shell --run 'bazel build //src/render:render'` passes with normal strict
warnings (`checks/build-r01.log`).

`nix-shell --run 'bazel test //tests:cove_hud --test_output=errors'` passes all
four cases, zero skips (`checks/tests-r05.log`, `tests-r05-test.log`,
`tests-r05.xml`). Its transitive dependencies also compile the current
Application and builder integration. Numerical GPU run uses AMD Radeon 890M /
RADV STRIX1 / Vulkan:

- Layout bounds cover 640x480, 960x540, 1280x800, 1920x1080, 3440x1440.
- Supported window layouts retain all short sample instructions and never enter
  the reserved build area or bottom palette. Oversized strings/tiny windows
  remain bounded and report truncation; text never shrinks to fit.
- Actual GPU output contains **3,675 text pixels**, **58,334 backing pixels**,
  **1,220 accent pixels**. An untouched scene sample remains [51,102,153].
- No validation errors. Discarding an encoder does not prevent the next draw;
  unchanged content does not re-upload. Releasing all HUD owners immediately
  after submission preserves the asynchronously read-back result.

Retained initial failures: r01 duplicate auto-generated test target (removed
manual duplicate), r02 missing test vector header, r03 builder action indentation
warning (fixed by builder agent), r04 pinned native shader compiler requiring a
mutable locally indexed array (changed `let corners` to `var corners`).

After the passing run, only paused empty-status wording changed to "Ready to
save" / "Waiting for motion to stop", and font-notice packaging entries were
added. Root's final native/CMake/WASM builds cover those changes. The renderer,
shader, font and test hashes in `source-hashes.json` match the passing run.

## Integrated application result

The actual 960×540 native journey places eight bricks with three tool choices,
rotates/stacks, stops, removes/undoes, launches without the spare preview, saves,
restarts, verifies the exact design, boards and sails. All 16 recorded stages
have encoded HUD quads, 20 px body text, no truncation and an in-window panel.
Actual completed fixture serials advance independently; they are not inferred
from the HUD encoding counter. See
`../../PLAY-02/continuous-r01/native-journey.json` and its process logs.

Independent read-only review found no actionable defect in the renderer, shader,
Application lifecycle/content/JSON integration, resource release, discarded-frame
reuse, format/coordinate mapping, bounded layout or camera/palette separation.
This is technical review, not visual approval.

Final browser compilation exposed Dawn's different vertex-attribute struct
layout. Explicit member assignments replace positional aggregate initialization;
native field values are unchanged. Both native and WASM rebuilds pass. The final
mandatory repository suite covers that source; the initial focused-test source
hashes remain separately labeled in `source-hashes-initial.json`.

## Remaining acceptance

The shared WASM application keeps its DOM UI and does not draw this native panel.
No full PLAY-02, LOOK-01 or parent gate
is claimed by the numeric overlay test. Font licensing is retained in source
and build packages; final release packaging must preserve that notice too.

## Combined checkpoint verification — 2026-09-12

The final combined source passes the required repository suite: 2,067 native
cases pass, three skip and four remain disabled; terrain import has ten passes
and one skip. Both actual-control construction/save/reload journeys pass.
[Checkpoint, source/package identities and full raw check results](../../checkpoints/2026-09-12-playable-cove.md)
record the current result. Earlier pending statements and original identities
above describe their historical stage. This checkpoint does not approve final
art or complete a game gate. The commit containing the checkpoint report is
the publication unit; normal repository hooks remain enabled.
