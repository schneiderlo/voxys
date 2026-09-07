# LEGO Shore in the Voxys engine

Open [Voxys](https://schneiderlo.github.io/voxys/): the main page now uses the
complete original 8192×8192 landscape with the shared LEGO geometry, grouped
brick material and palette. No URL option is needed. `?experience=lego-world`
is an explicit link to the same scene. Native: `--config lego_world.cfg`.

The smaller 256×256 shoreline study remains at `?experience=lego` (native:
`--config lego_shore.cfg`). `?experience=terrain` opens the original smooth
landscape; K now enables the same LEGO surface there, including collisions.
`?experience=ridgebreak` retains the riding scene.

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

Both launchers select 256×256 for LEGO Shore and 8192×8192 for LEGO World. The application also preserves
the loaded LEGO source dimensions rather than interpolating to a generic 8K
target. Fallback color maps are capped at 256×256 (256 KiB), and this scene
explicitly clears inherited landscape image paths. The browser heap remains
fixed at 512 MiB.

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

## Full landscape cache

The world reuses the exact same 32×32 chunk builder as the shore study. A
1024×1024 R32Uint toroidal texture uses 4 MiB, regardless of the source map size.
The CPU tracks at most 1024 chunk requests and resident tags in under 20 KiB,
with a 4 KiB upload buffer. Each frame builds at most four missing chunks,
nearest first. Crossing a chunk boundary queues only missing rows; a stationary
camera does no more layout work once its window is resident.

Every record carries a world chunk tag. Wrapped slots from another location
are rejected, including after teleports. Until a chunk is available, and beyond
the cache window, the shader uses single-cell seams and the same twelve-color
palette, classified from the existing height texture. Grouping changes only
material ownership: ray geometry and physical contacts never depend on cache
residency. Uploads invalidate cached background color without recreating bind
groups or retracing unchanged geometry.

K compares grouped/single appearance in either dedicated LEGO scene. On the
smooth landscape, K switches both rendering and WebGPU terrain collision.
The capsule mover reuses its existing sample allocation during the switch to
avoid a second temporary 128 MiB copy. Other physics backends cannot activate
LEGO collision. Sphere play remains the supported interactive shape; arbitrary
shape fidelity and terrain editing remain separate work.

## Validation

The release WASM build uses Emscripten 6.0.1, matching Pages CI. C++ tests cover
all 65,536 input heights, deterministic chunk ownership, unchanged samples,
stud/step contacts, capsule support, walking over chunk boundaries, cliffs and
jumping at negative world heights. Existing capsule tests remain in the gate.

Before publishing, Pages runs `python3 scripts/check_wasm_lego_assets.py build-wasm`.
It extracts the shoreline and original 8K terrain from the generated preload
package, checks that the bytes match the source assets, and loads both using
the built engine's WebAssembly objects under Node. Every shoreline sample must
match the original crop. This startup check does not require a graphics adapter.

Pages then opens `/` in Chrome with software WebGPU and runs the full application
at 960×540. The report checks 12 frames with completed GPU work, no browser/GPU
errors, visible LEGO controls, 8192×8192 terrain with fourteen mip levels, and the
unchanged 512 MiB heap. It saves a report and a screenshot on success. The existing Pages workflow
reports SwiftShader startup failures as warnings; native and isolated GPU
regressions remain blocking checks.
This is a startup regression check, not a hardware FPS measurement.

LDH writes use standard IEEE CRC32. Reads also recognize the historical checksum
from older native writers, whose table contained one incorrect entry. Both
variants remain checked; corrupt payloads and checksum footers are rejected.

```sh
python3 scripts/sync_lego_surface.py --check
python3 scripts/test_lego_surface_gpu.py
python3 scripts/test_lego_rendering.py
VOXY_LEGO_STUDY=1 python3 scripts/test_lego_rendering.py
VOXY_LEGO_MODE=4 python3 scripts/test_lego_rendering.py
python3 scripts/test_lego_layout_gpu.py
```

GPU tests require numpy, wgpu and a Vulkan adapter. They check every plate
height, analytic contact cases, fast impacts and sweep exhaustion. Independent
box/cylinder ray intersections validate complete mip traversal in legacy and
shared-surface modes, including grazing rays, distance and maximum height.
Production ray and background material shaders were also rendered offscreen
with the real crop. SwiftShader results establish correctness; they do not
establish integrated or mobile hardware FPS.

World regression coverage includes the 8192² edge chunk, cache row reuse,
teleport eviction, fallback palette parity, grouped seams, and live character
surface switching. Refactoring the builder preserves the old study layout
byte for byte on 33, 65, 256 and 512 sample fixtures.
