# Flat deck shadow correction

The shared sun filter now compares each tap with the geometric receiver plane
at that tap, instead of reusing the center depth. This removes sloped
self-shadowing on broad dock panels without increasing global contact bias.
The orthographic matrix's inverse-transpose plane coefficients include UV Y
inversion. Near-parallel planes retain the previous reference.

The 1024² map, 48 m extent, 128 m depth range, 3×3 bilinear kernel, border fade,
normal bias, 3 mm constant bias and caster raster bias are unchanged. The
correction is shared by mesh, terrain and water, including legacy routes.
No ABI, GPU allocation, owner budget, draw count, live-body transform or
submission/retirement contract changed. The template and its three generated
consumers pass `scripts/sync_scene_shadows.py --check`.

## Measured result

The [new numerical GPU case](shadows/checks/gpu-r02.test.xml) rasterizes an
actual horizontal receiver and a 6 cm elevated caster with production depth
bias. Independent ray/plane intersections provide 256 clear subtexel phases
and 64 contact-shadow samples. The exact shipping filter and a test-only
mutation restoring the old constant reference run against the same depth map.

| Measurement | Result |
|---|---:|
| Corrected minimum clear visibility | 1.0 |
| Old minimum clear visibility | 0.890625 |
| Old samples with false shadow | 96 / 256 |
| Corrected maximum visibility under 6 cm caster | 0.0 |

Both [existing Blit composition cases](shadows/checks/blit-r01.test.xml) also
pass after this shared change. The legacy thresholds remain intact. Cove's
shadowed linear seabed red decreases from .249267578125 to .069580078125,
matching its ambient-only floor; visible water red decreases from 107 to 79.
The moving-shadow, camera/static-depth and shared filtered-environment checks
remain active. This is three focused cases passed, zero skips, on native
Vulkan / AMD Radeon 890M. It is not a full-suite or browser-runtime result.

The [initial failed test log](shadows/checks/gpu-r01.test.log) is retained:
the new depth-only test descriptor omitted required stencil defaults, causing
a native descriptor-conversion abort before measurement. Explicit Always/Keep
stencil defaults fixed that test. Production shader code did not change between
that failure and the passing run. [Source hashes and exact properties](shadows/checks/summary.json)
pin the tested inputs. [Independent source review](shadows/checks/independent-review.md)
checks the plane math, resource lifetime and test oracle separately.

## Initial rejected visual review

The previously supplied actual `assembled-native-r01.png` at 1280×720 showed
clear progress in the cream/teal/orange robot, structural kit and machinery.
Exposed studs and seams identify construction-toy geometry. The complete
playable scene is a substantial change from the landscape-only prototype.
The [summary](shadows/checks/summary.json) identifies that exact image by hash;
no picture was produced for these shadow checks.

That initial image was **not accepted for LOOK-01**. The dock dominated the
foreground; boat, robot, cargo and gantry overlapped; the workshop and beacons
were behind the camera; the horizon lacked a clear destination. Its associated
state was only completed/presented tick6, so the boat was still in its initial
fall. It cannot establish the settled waterline or final composition. Broad
cream panels also had periodic diagonal shading ramps despite solid PBR
materials and distinct panel/core top elevations. The numerical mutation
control above confirms a real constant-reference shadow defect.

A later actual arrival must show the settled whole skiff, clear player/dock
activity and the harbor context at declared settings. Saved views and physical
parts must remain intact. Matching native/browser evidence and a moving view
remain root's separate acceptance work. This correction alone does not approve
the backdrop, final materials, performance, full visual gate or AAA quality.
