# Continuous LOD inspection — browser evidence

**Historical browser-only checkpoint:** the subsequent [matched native/browser
recording](native-motion.md) now supplies both current captures and regression
evidence. The original records below retain package-07 identities and their
then-pending native work.

This completes the first browser camera-motion recording. The matching native
recording and independent technical review remain open. ASSET-04, LOOK-01 and
G02 are not passed. See `assembly.md` for the exact admitted six-part fixture
and `GAME_IMPLEMENTATION_TODO.md` for all remaining work and commit policy.

## Actual workload

The unchanged shipping package `/tmp/voxys-asset-browser-07` runs its normal
`salvage-assembly` route at 1920×1080 physical pixels, DPR 1 and FOV 60. The
camera retreats from 6 to 100 metres and returns over 30 seconds. The path in
`lod-motion.json` uses logarithmic distance and smoothstep easing on each half.
Only the existing camera-pose API is driven. No LOD, build state, successful
result, shader or rendering workload is injected.

The four cooked pontoons and two shared prototype crossbeams remain present.
Automatic LOD selection uses the actual C++ projected bounds, with the
unchanged 200/60/0-pixel thresholds. The two prototype LOD entries remain null.
The session has zero builds, inventory grants and scenery bodies throughout.

`scripts/validate_salvage_asset_motion.mjs` bounds its trace to 8192 samples and
captures to 256 images. It uses a fresh browser and records every observed
camera/LOD state, frame capture time brackets, original JPEG hashes, actual
upload counts and completed GPU work. Captures add overhead; this is not a
performance benchmark. A reported camera state and JPEG are not claimed to be
an atomically sampled renderer frame: the original timing brackets are kept.

## Observed result

Evidence: `lod-browser-motion-attempt01/report.json`, `trace.json`, 128 original
JPEG captures, and the separate startup report. All four pontoons follow
**1 → 2 → 3 → 2 → 1**, without intervening oscillation. The trace contains
1778 samples over 30.0042 seconds. The complete model draw count stays six;
the three cooked uploads, one prototype upload and 6264556-byte conservative
reservation remain constant. Completed GPU serial advances from 196 to 1982.
No application failure, device loss or uncaptured GPU error is reported.

The review video `lod-browser-motion-attempt01/motion.webm` is encoded and
decode-checked: exactly 30.0 seconds, 360 output frames, 1920×1080 VP9, 21.41
MiB. `integration/motion-summary.json` binds the recipe, runners, source trace,
video and original assembly package by SHA-256. The application binaries,
shaders, content and page are unchanged from the assembly checkpoint; only
the external capture harness adds the motion option.

| Placement | Receding 1→2 | Receding 2→3 | Returning 3→2 | Returning 2→1 |
| --- | ---: | ---: | ---: | ---: |
| 0 | 3.900 s | 8.681 s | 21.374 s | 26.151 s |
| 1 | 4.548 s | 8.841 s | 21.213 s | 25.509 s |
| 2 | 5.070 s | 8.999 s | 21.057 s | 24.979 s |
| 3 | 4.676 s | 8.877 s | 21.182 s | 25.382 s |

These are observed times on this run, not portable distance thresholds. Parts
switch at different times because their bounds occupy different projected
sizes. Threshold correctness and arbitrary stable LOD IDs have separate shared
CPU tests; this capture establishes that the application uses those LODs
during continuous camera movement.

Root inspected original frames 0000, 0015, 0017, 0036 and 0038 around the near
view and first two transitions. The overall assembly and stack silhouettes
remain legible in those samples, with no missing part or separated join.
Sampling about four JPEGs per second cannot exclude a one-frame defect or
replace independent moving-image review. Prototype crossbeams and the
inspection backdrop still do not establish the game's visual direction.

## Replay and review video

From the repository root, after packaging the ordinary optimized WASM app:

```sh
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_REPORT=/new/startup.json VOXY_SMOKE_SCREENSHOT=/new/startup.png VOXY_SMOKE_ASSET_MOTION=/new/motion node scripts/smoke_integrated_wasm.mjs /absolute/path/to/package salvage-assembly
python3 scripts/encode_inspection_motion.py --input /new/motion
```

Run Chrome outside the Nix GPU-library environment. The encoder requires
FFmpeg/ffprobe with VP9 support. It verifies original image hashes, holds each
captured frame according to its measured midpoint time, encodes a 30-second
1920×1080 VP9 video at 12 FPS, then decodes/counts every output frame. It does
not synthesize in-between camera images; 12 output FPS is not 12 unique
captures per second. `video.json` records the exact encoder, arguments,
timestamps, source hash and decode result. Original frames remain available.

The next motion work must capture this declared path through the actual native
application, preserve matching camera/light/resolution and review both moving
captures. Do not substitute a series of native restarts for continuous motion,
enable the unrelated benchmark mode (which excludes salvage), or infer a native
result from this browser recording. The authored hierarchy stand, rendered
rotation matrix, final reproducible candidate and independent review also
remain required by ASSET-04.
