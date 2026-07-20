# Spectral Ocean

Voxys uses a deterministic, peaked directional spectrum. The complete animated
simulation runs on WebGPU for both native and WASM builds.

## Cascades

| Cascade | Patch | Wavelength band | Purpose |
|---|---:|---:|---|
| 0 | 1949 m | broad band | wind sea and distant silhouette |
| 1 | 326 m | detail band | short waves and crest detail |

Each cascade is 256×256. A spectral update performs:

1. Quantized deep-water dispersion evolution: `omega = sqrt(g * |k|)`.
2. Eight horizontal inverse-FFT stages.
3. Eight vertical inverse-FFT stages.
4. Resolution into three-axis displacement, normals, and compression.

The resolved `RGBA16Float` array contains two displacement layers and two
normal layers. Four analytic long swells are added to the spectral result. The
same combined surface drives rendering, buoyancy, and water contact sampling.
Spectral state advances at up to 120 Hz; clipmap geometry, analytic swells,
material, refraction, and presentation still execute for every rendered frame.

## Geometry clipmap

The ray renderer produces opaque HDR color and linear depth first. The ocean is
then drawn as real indexed geometry into those same color/depth targets:

- five camera-following 64×64 clipmap levels, beginning at an 800 m patch;
- holes in outer levels so tessellation is concentrated near the camera;
- four stretched outer strips extending the surface to 47.5 km;
- vertex displacement from both live FFT cascades and all four long swells;
- terrain-height rejection at coasts and exact opaque-depth occlusion.

This path remains authoritative when the camera moves. Camera motion refreshes
the opaque scene cache; it does not select the older fullscreen water shader.

## Rendering

The geometry fragment material samples the resolved broad/detail normals and
adds the analytic long-wave normal. It then applies:

- exact unpolarized dielectric Fresnel and underside TIR at IOR 1.31;
- sun-specular and subsurface-scatter lobes;
- screen-space distorted refraction with Beer–Lambert absorption;
- a generated tileable foam field using the material threshold formula;
- a generated HDR cloud environment with a complete roughness mip chain;
- a generated tileable sand/rock/shell seabed material with animated caustics
  where water extends beyond the finite heightfield;
- distance-dependent reflection roughness and fog;
- ACES, film grain, and vignette presentation;
- underwater absorption, animated distortion, sun shafts, and 1000
  depth-occluded suspended-particle billboards.

The generated material packs whitecap coverage and seabed albedo into one
1024² mipmapped texture. An original environment image is decoded to linear
light and baked once to a 512² full-sphere HDR LUT with a complete roughness
mip chain. The water and seabed themselves require no bitmap or model.

The full geometry/material path remains active while the camera moves. When
the camera is stationary, only opaque terrain/background work is cached; the
water simulation and clipmap draw continue every frame.

## Performance reference

WASM, AMD Radeon 890M (RDNA 3), Chrome 149, Vulkan/ANGLE, 3440×1454
full-resolution target, 8192² terrain:

- three runs of 1,500 frames across five standard camera views;
- 944.76, 944.52, and 949.73 FPS;
- 944.52 FPS minimum and 946.33 FPS aggregate;
- all 4,500 measured frames confirmed retired by the WebGPU queue.

The benchmark keeps the complete geometry/material and physical-resolution
output. It includes terrain ray-casting, spectrum evolution, clipmap drawing,
composition, primitives, and WebGPU physics. It renders to a physical-size
offscreen target and waits for queue completion after every batch, so it
measures renderer throughput rather than monitor refresh or raw submissions.

The complete `//:voxy_wasm` application uses this same geometry and material;
the ocean path does not rely on native-only GPU features.
