# Matched continuous LOD captures

The native and browser motion-recording checkpoint is complete. ASSET-04,
LOOK-01 and G02 remain open. Independent moving-image/technical review,
the authored hierarchy/winding stand, rendered rotation fixtures and final
candidate publication are still required. This is static asset inspection,
not playable boat physics or accepted game art.

## Shared scene and result

Both applications use `lod-motion.json`, `salvage_assembly_fixture.cfg` and the
unchanged v2-r04 pontoon bundle selected by `fixture-pontoon-assembly.json`.
The exact six-part/five-weld assembly is documented in `assembly.md`.
The camera retreats from 6 to 100 metres and returns over 30 seconds, using
logarithmic distance with smoothstep easing on each half. Physical viewport
is 1920×1080, FOV 60, and camera, daylight and exposure settings match.
Native logical size is 1536×864 on this scaled host; browser is 1920×1080/DPR 1.
The browser's HTML controls/FPS overlay do not exist in native captures.

| Evidence | Native | Current browser |
| --- | --- | --- |
| Directory | `lod-native-motion-attempt01/captures/` | `lod-browser-motion-attempt02/` |
| Actual state samples | 1801 | 1719 |
| Recorded motion interval | 30.014923 s | 30.009700 s |
| Original JPEG captures | 189 | 126 |
| Every pontoon's observed LOD sequence | 1→2→3→2→1 | 1→2→3→2→1 |
| Model draw count throughout | 6 | 6 |
| Cooked/prototype uploads throughout | 3 / 1 | 3 / 1 |
| Conservative asset GPU reservation | 6264556 bytes | 6264556 bytes |
| Completed GPU serial, start→end | 3→1809 | 197→1925 |
| Review video | 30 s, 1920×1080 VP9, 360 decoded frames | Same |

The prototype beams retain null LOD entries. No level is forced, no accepted
build/inventory is injected, and the live inspection session stays empty.
Neither run reports a missing model, application/GPU validation failure or
increasing asset residency. Browser HTML includes prototype limitations.

Observed transition times agree closely but are not asserted to be identical:
the browser observes its state around animation callbacks, while native
records state after actual render submission. Readback and image encoding
also consume time. Reports preserve pose time and capture time brackets.
Camera-state/image pairs are not claimed to be cross-platform atomic samples.

Root inspected native original frames 0000, 0023, 0025, 0053 and 0055, and
current browser frames 0015, 0017, 0035 and 0037 around the first two transitions.
The assembly/stack remain coherent and the sampled silhouettes agree; no part
disappears or separates at those samples. This limited sampling cannot exclude
a one-frame defect or substitute for independent review of the moving images.
No final visual approval is recorded.

## Native implementation and bounds

The native executable accepts an explicit paired request:
`--inspection-motion <recipe.json> --inspection-motion-output <new-directory>`.
Incomplete requests, another active capture/benchmark mode, a different
registry/FOV, invalid motion bounds or an existing output directory reject
before GPU startup. The browser entry rejects these native-only arguments;
browser recording continues to use its external camera runner.

`InspectionMotion` lives in `src/engine/platform/native/inspection_motion.hpp`.
It uses the existing update callback to set only the camera, matching the
browser's world-sector convention and final float conversion. A new empty-by-
default capture callback runs after normal render submission and before
presentation inside `Application::processFrame`. The normal render pipeline
and terminal-update guard remain active. Capture overhead is included in wall/CPU frame
timing, so these recordings cannot certify performance.

`Application::captureScreenshot` now returns actual readback/write success and
supports explicitly selected JPEG output for motion. Existing callers retain
PNG as the default. It copies the real submitted surface and waits for its
real readback; no replacement renderer or screenshot-only mesh path is used.
Callbacks are removed before native shutdown.

The recorder admits 1..65536-byte input files, 10..60-second paths, at most
256 images and 8192 state samples, plus a 16 MiB bound on serialized trace
payload. It checks actual physical size throughout the run and requires every
part to traverse exactly 1→2→3→2→1. A final near-view submission must finish on
the GPU before the recording reports success. Closure, exceptions, capacity
failure or failed readback produce a failed record rather than a partial pass.
These are tooling limits, not the game's final content/performance budgets.
Temporary screenshot readback/encoding storage and driver allocations are
outside the quoted asset-owner reservation.

`capture_salvage_asset_motion.py` runs one uninterrupted native process, verifies
normal exit, source/binary stability, every original JPEG's size/hash, the LOD
sequence and original trace. It preserves `native-report.json`, then creates a
separate `report.json` with verified image hashes. No source registry is edited.
`encode_inspection_motion.py` verifies input hashes, holds captured frames at
their measured midpoint times, encodes both videos and decodes/counts all 360
output frames. Twelve output FPS does not mean twelve unique images per second;
there is no synthetic interpolation. Original captures and timings are retained.

## Verification and reproduction

- Optimized Bazel: 59 configuration, 18 owner/guide and 6 normal MeshPath cases
  pass with no skips. Both the native app and final optimized WASM build pass.
- CMake: native app and tests build; 118 explicitly filtered configuration,
  application-configuration, guide/owner and MeshPath cases pass with no skips.
  This is not a new full repository gate-suite run.
- Six real native CLI rejection cases preserve existing output and reject
  before GPU startup (`integration/native-motion-cli.json`).
- The CMake app's ordinary PNG/socket-X-ray capture passes with no capture
  callback enabled (`native-motion-png-regression-attempt01/`).
- Current browser package `/tmp/voxys-asset-browser-08` passes real detail/guide
  controls, resize, Reset, drained Leave and re-entry in
  `native-motion-browser-journey-attempt01/`. A separate real GPU-device
  destruction with 150 guide boxes active completes exceptional shutdown in
  `native-motion-browser-loss-attempt01/`.

`integration/native-motion-summary.json` binds the current sources, tests,
binaries, packaged browser files, reports, images and videos by SHA-256.
Earlier package-07/browser-only records in `motion.md` retain their historical
identities. The first native compile failure exposed optional-path handling;
the corrected source builds without disabling warnings. The first package
command used the wrong target name; `make package-wasm` is the verified target.

Run from the repository root in the native graphics environment:

```sh
bazel build -c opt //:voxy_native
python3 scripts/capture_salvage_asset_motion.py --binary bazel-bin/voxy_native --output /new/native-recording
python3 scripts/encode_inspection_motion.py --input /new/native-recording/captures
python3 scripts/validate_native_motion_cli.py --binary bazel-bin/voxy_native --report /new/cli-report.json
```

The encoder requires FFmpeg/ffprobe with VP9 support. On this host it was run
through `nix-shell -p ffmpeg-full`; exact encoder versions and arguments are
recorded in each `video.json`. Run Chrome outside the Nix GPU-library environment:

```sh
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_REPORT=/new/browser-startup.json VOXY_SMOKE_SCREENSHOT=/new/browser-startup.png VOXY_SMOKE_ASSET_MOTION=/new/browser-recording node scripts/smoke_integrated_wasm.mjs /absolute/path/to/package salvage-assembly
python3 scripts/encode_inspection_motion.py --input /new/browser-recording
```

Use fresh output paths. The native motion recorder validates this declared
six-part assembly; it is not a general world traversal tool. Extend it explicitly
with new bounded scene contracts if later validation requires other workloads.
