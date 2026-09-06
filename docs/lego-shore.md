# LEGO Shore in the Voxys engine

Open [LEGO Shore](https://schneiderlo.github.io/voxys/index.html?experience=lego).
The island prototype also links to it. Native builds accept `--config lego_shore.cfg`.

Walk with WASD, look with the captured mouse, and jump with Space. Click or press
B to throw a ball; the button also works on touch screens. F8 switches between
walking and flying. K compares grouped bricks and individual cells. Reset reloads
the scene and clears the balls. Only FPS and the compact scene controls appear;
the existing URL debug option reveals the diagnostic panels.

## One physical surface

This scene contains an unfiltered 256×256 crop of the original 8192² landscape,
starting at sample (2816, 7424). `data/lego_shore.json` records the source and raw
sample checksums. Reproduce it with `python3 tools/extract_lego_shore.py` (numpy
and zstandard required). Height scale is 600, cell pitch 1, and water height −200.

Raw samples stay unchanged. Integer arithmetic maps them onto 3750 plate levels
across the original height range: 0.32 units per plate, radius 0.30 and height
0.18 for each stud. The monotone transformation preserves the conservative
maximum-height pyramid used by ray traversal. Columns and capped cylindrical
studs are analytic solids. Bevels and decorative seams affect shading only.

`src/terrain/lego_surface.hpp` defines CPU queries, capsule support, and layout.
`shaders/lego_surface.wgsl` defines the equivalent GPU contact query and integer
quantization. `scripts/sync_lego_surface.py` embeds this canonical code in the
standalone ray, material, rigid-body and CCD shaders. CI rejects stale copies.

Player motion and WebGPU sphere contacts use this stepped surface, including
studs and vertical terrace edges. Character steps use a raised forward cast and
exact lower-hemisphere support. Sphere sweeps use conservative advancement,
visit a one-cell halo, and advance at most half a cell. After 256 iterations a
sweep stops at its last safe pose rather than skipping unseen terrain.

## Bricks and color

Layout is built once, in fixed 32×32 chunks. Largest-first passes combine only
cells on the same plate level into 2×4, 4×2, 2×2, 1×2, 2×1 and 1×1 footprints.
Bricks stay within a chunk. Stable source coordinates determine orientations
and shades. The current crop contains 27,805 bricks, including 2,224 eight-cell
bricks, in 64 chunks.

Each cell stores one 16-bit ownership record: offsets, dimensions and palette
index. The complete layout texture is 128 KiB. It is queried only after a ray
hit. No terrain mesh or individual brick physics bodies are created.

Twelve colors reuse the island study's sand, meadow, forest and warm stone
families. A whole brick receives its majority terrain family. Studs, tops and
sides share that color. Seams follow the owning brick; internal cell boundaries
do not receive artificial joints. Distant shade variation and bevels fade with
pixel footprint, and roughness increases. Geometry and collision remain fixed
throughout both grouping modes.

## Performance limits

- The renderer retains hierarchical traversal and cached terrain depth, normals,
  shadows and background color. Animated water continues through the existing
  water renderer. A stationary camera does not retrace the terrain each frame.
- The compact scene allows at most 32 balls. Held fire is limited to about three
  balls per second; right-click batches contain at most eight. Balls share the
  existing GPU physics and instanced primitive rendering paths.
- Layout construction measured about 3.6 ms once in the development container.
  This is a CPU setup measurement, not a frame-time promise.
- The normal application still preloads its other scenes and assets. This change
  reduces the active terrain workload; it does not reduce the shared WASM
  download or its reserved memory.

This is a bounded playable integration, not streaming LEGO conversion of the
whole 8K world. It explicitly requires the WebGPU physics backend and exposes
sphere play. Jolt/Box3D terrain adapters, arbitrary shape fidelity, editable
chunks and full-world streaming remain separate work. Unsupported backends fail
attachment rather than silently using the smooth heightfield.

## Validation

The release WASM build uses Emscripten 6.0.1, matching Pages CI. C++ tests cover
all 65,536 input heights, deterministic chunk ownership, unchanged samples,
stud/step contacts, capsule support, walking over chunk boundaries, cliffs and
jumping at negative world heights. Existing capsule tests remain in the gate.

```sh
python3 scripts/sync_lego_surface.py --check
python3 scripts/test_lego_surface_gpu.py
python3 scripts/test_lego_rendering.py
VOXY_LEGO_STUDY=1 python3 scripts/test_lego_rendering.py
```

GPU tests require numpy, wgpu and a Vulkan adapter. They check every plate
height, analytic contact cases, fast impacts and sweep exhaustion. Independent
box/cylinder ray intersections validate complete mip traversal in legacy and
shared-surface modes, including grazing rays, distance and maximum height.
Production ray and background material shaders were also rendered offscreen
with the real crop. SwiftShader results establish correctness; they do not
establish integrated or mobile hardware FPS.
