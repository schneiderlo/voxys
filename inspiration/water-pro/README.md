# Three.js Water Pro Inspiration

This folder is for reference notes only.

Do not commit copied Three.js Water Pro shader source here unless the project has
an explicit license that allows redistribution.

If a licensed local copy is available, keep it under:

```text
inspiration/water-pro/vendor/
```

That folder is ignored by git.

## Sources

- Live demo: https://www.threejswaterpro.com/
- Product page: https://threejsroadmap.com/assets/threejs-water-pro
- Docs: https://docs.threejswaterpro.com/

## Visual Target

Water Pro succeeds because the water is a full material system, not a blue plane.

Important traits to reproduce in `shaders/ray_blit.wgsl`:

- Depth absorption: shallow water is bright and transparent; deep water is dark and saturated.
- Fresnel: low camera angles become reflective; steep view angles show more bottom color.
- Multi-scale waves: large rolling swells plus smaller wind ripples.
- Crest/foam layers: whitecaps, ambient surface foam, and shoreline foam are separate signals.
- Caustics: shallow water adds moving bright patterns on the seabed.
- Atmosphere: water color fades into fog/horizon color over distance.
- Quality levels: expensive features should be easy to disable or simplify.

## Native WGSL Mapping

We are not using Three.js, mesh clipmaps, or FFT compute passes right now.

The closest native version should use:

- Analytic spectral waves in the fullscreen water pass.
- A normal from summed directional waves.
- A crest scalar from wave slope/phase.
- Depth-based color absorption from `terrainDistance - waterDistance`.
- Fresnel reflection using the sky color and cloud coverage.
- Shore foam from `planeMask`, `nearShore`, and noisy breakup.
- Cheap caustics only when `waterDepth` is shallow.

## Current Gap

The current renderer now has waterline screenshots and basic water shading.

Still missing:

- Real wave displacement or convincing parallax.
- Strong reflected scene/sky energy.
- Fine foam breakup.
- Bright shallow-water caustics.
- Better water camera framing with foreground objects.

## Implementation Notes

Short-term changes should stay in `shaders/ray_blit.wgsl`:

- Replace single `distanceRipple` with a reusable wave-spectrum helper.
- Use wave steepness for foam, not only random noise.
- Make near water more transparent and far water more reflective.
- Add a water-quality switch later if cost grows too much.

Longer-term changes need renderer support:

- A separate water pass with real surface geometry.
- Screen-space reflection or planar sky/reflection sampling.
- Optional compute wave texture for FFT-like motion.
