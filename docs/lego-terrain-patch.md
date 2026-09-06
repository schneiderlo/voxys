# Grouped LEGO terrain study

Open `lego_patch.html` beside the deployed Voxys `index.html`. Drag to orbit,
scroll or pinch to zoom, and tap the island or use **Drop a ball**. Compare
**Grouped bricks** with **1 × 1 cells**. Reset clears the balls and camera.

This is the first isolated patch prototype, not a replacement for K mode or the
full landscape. It does not modify the production heightmap or any physics
backend. GitHub Pages already copies every file from `web/` into the deployment.

## Shared terrain representation

- A deterministic 32 × 32 heightmap uses integer plate levels: stud pitch 1,
  plate height 0.32, stud radius 0.30 and stud height 0.18.
- A greedy, deterministic cover combines only equal-height, unoccupied cells
  into 1×1, 1×2, 2×1, 2×2, 2×4 and 4×2 footprints. Every land cell has one owner.
- The island has 620 land cells, represented by 265 grouped bricks. Changing
  the grouping leaves heights and studs identical.
- Geometry emits grouped tops and only exposed side segments. Stud meshes are
  generated only on land. Seams, colour variation and bevel shading use the
  owning brick, so merged interiors do not acquire false cell-sized joints.
- Sphere contacts use the same brick bounds, plate heights and stud dimensions.
  The cylinders are analytic for collision and 16-sided for rendering; bevels
  affect shading only. This is a small toy contact solver, not engine physics.

## Material palette

Twelve authored sRGB colors form four coordinated families: warm sand, meadow
and forest greens, and warm stone. A brick receives the majority terrain family
across its whole footprint, considering plate height and nearby relief. A small
rocky flank is kept coherent rather than scattering grey noise through grass.
Three nearby shades per family are selected by stable position hashing, with
the middle shade used most often. Top faces, sides and studs all share the
resulting palette index and base color. Lighting supplies their differences.

Colors are converted to linear light once and written into the existing vertex
color field when the static mesh is built. There are no additional texture
samples, rendering passes, vertex attributes, or per-frame palette calculations.

## Performance choices

The CPU constructs the static vertex buffer only on initial load or a grouping
change. The terrain shadow map is 1024² and refreshed only when that mesh changes.
Balls use a shared sphere mesh and instance transforms (16 balls maximum), with
a fixed 120 Hz physics step, local terrain queries, and bounded pair checks.
Single settled balls sleep. Rendering stops when the camera and balls are idle;
**Idle** replaces the FPS count until the next input. Hidden pages pause work.

Rendering is capped at 1.6 million internal pixels and DPR 1.5 with 4× MSAA.
Fine bevel shading fades with screen footprint. This preserves narrow-edge
stability without increasing terrain geometry at a distance.

## Validation

```sh
node scripts/test_lego_patch.mjs
npm install --no-save webgpu
node scripts/test_lego_patch_gpu.mjs
```

The GPU test needs a Vulkan WebGPU adapter; SwiftShader is supported. It executes
the actual page shader and rendering code with an offscreen canvas adapter,
checks nonblank terrain output, both grouping modes, the sphere-instance limit,
mobile dimensions, reset, and hidden-page handling. It is not a browser DOM test
or a physical-GPU FPS benchmark.

CPU tests cover deterministic ownership, full coverage without overlapping
bricks, identical surface heights between grouping modes, sphere support on
brick tops/studs, sleeping, and sphere-pair separation.

## Before expanding to the full landscape

The full engine has multiple terrain collision backends that currently consume
the original smooth heightmap. A production integration must introduce a shared
quantized terrain/brick resource for those backends and rendering, including
chunk edits and rebuilds, rather than changing shader heights independently.
The next integration should retain the existing hierarchical ray traversal and
cache compact per-chunk brick ownership instead of expanding the entire 8K map
into the demonstration's triangle mesh. Full-world triangle expansion is not
the proposed scaling strategy.
