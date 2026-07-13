# FFT Ocean

Voxys uses a three-cascade Tessendorf ocean. The simulation is generated from
a deterministic directional Phillips spectrum and updated entirely on the GPU.

## Cascades

| Cascade | Patch | Wavelength band | Purpose |
|---|---:|---:|---|
| 0 | 96 m | 1.5–36 m | capillary detail and breaking crests |
| 1 | 384 m | 18–150 m | local wind waves |
| 2 | 1536 m | 76–900 m | swell and distant silhouette |

Each cascade is 256×256. Every frame performs:

1. Deep-water dispersion evolution: `omega = sqrt(g * |k|)`.
2. Eight horizontal inverse-FFT stages.
3. Eight vertical inverse-FFT stages.
4. Resolution into height, slope, and horizontal-displacement Jacobian.
5. Foam advection, accumulation, and exponential decay.

The resolved `RGBA16Float` array stores:

- R: surface height.
- G/B: analytic height slope.
- A: Jacobian compression used for breaking-wave detection.

## Directional coasts

A static 1024² coastal field is built from the terrain heightmap when the
ocean starts. It stores water depth, distance to land, the nearest shoreline
direction, and incoming-wave exposure.

The exposure pass follows the dominant swell direction. Terrain blocks energy
down-wave, producing sheltered water behind islands and headlands. The shadow
then decays over distance to approximate diffraction around their edges.

Inside the coastal band, wave phase transitions from the offshore swell axis
to distance-to-shore contours. This makes exposed crests slow, turn parallel
to the beach, shoal, and break. Leeward shores retain much less displacement
and foam instead of responding equally around the entire island.

## Rendering

The ray-caster and lighting pass sample the same displacement texture. This
keeps the visible normal aligned with the surface hit instead of layering
unrelated normal noise over a flat plane.

The lighting pass adds:

- physical air/water Fresnel;
- GGX sun reflection;
- Snell-law bed refraction;
- FFT-slope-derived refracted caustics;
- persistent whitecaps sourced from Jacobian compression;
- shoreline foam;
- windward breaking and leeward wave shelter;
- distance-aware cascade filtering.

## Performance reference

AMD Radeon 890M, Vulkan, 1280×720, 8192² terrain:

- 1,500 frames across five standard benchmark views.
- 78.1 FPS overall average.
- 72.7 FPS slowest scenario.
- 84.7 FPS fastest scenario.

The benchmark includes the terrain ray-caster, FFT simulation, foam update,
lighting pass, and presentation.

The complete `//:voxy_wasm` application also builds with the same simulation;
the ocean path does not rely on native-only GPU features.
