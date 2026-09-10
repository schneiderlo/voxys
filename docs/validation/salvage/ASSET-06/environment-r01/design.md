# Environment-lighting producer

Status: scoped ASSET-06 preparation under D18. The producer is implemented;
the companion validation report records executed checks. It is **not yet bound
by MeshPath or the application**, so no game-view improvement is claimed.
REND-01–04 and LOOK-01 remain open.

## Why this is needed

The [actual native material ablation](../detail-diagnosis-r01/README.md) retains
the strongest streaks without either detail map. The existing mesh shader uses
ordinary equirectangular image mipmaps as rough reflections and the smallest mip
for diffuse lighting. Those are spatial box averages rather than directional
BRDF convolution. This producer adds the missing lighting data without changing
the original environment used by terrain/water.

The split-sum approximation, GGX roughness convention and separate environment
visibility remap follow Brian Karis's primary
[Epic SIGGRAPH 2013 shading notes, pages 3–6](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf).
The WGSL and C++ are original implementations. Specular prefiltering assumes
the view and normal align with the reflection direction; this approximation
does not reproduce elongated grazing highlights. Diffuse uses a cosine-weighted
hemisphere. Neither output includes display transforms or baked direct sun
occlusion.

## Data and equations

All inputs/outputs are linear radiance. Source: filterable equirectangular 2D
texture, preferably with full ordinary image mips. Longitude wraps in U;
latitude clamps in V. +Y is up, and longitude is `atan2(z,x)`. The source's
existing radiance scale is retained; this producer does not infer HDR lighting
from an LDR photograph or apply exposure.

| Default output | Format and extent | Texel bytes |
|---|---|---:|
| Specular | RGBA16F cube, 128² per face, 8 mips | 1,048,560 |
| Diffuse `E/pi` | RGBA16F cube, 32² per face, one mip | 49,152 |
| Reflection coefficients A/B | RGBA16F 2D, 128² | 131,072 |
| Ten immutable parameter uniforms | 16 bytes each | 160 |
| Total requested content | Excludes opaque driver/handle overhead | 1,228,944 |

RGBA16F storage avoids depending on optional storage formats. Output alpha is
one. The BRDF texture stores A in R, B in G, zero in B. Each output mip has its
own immutable uniform buffer; no queue write can overwrite parameters for an
earlier dispatch in the same submission.

Perceptual roughness `r` maps to GGX `alpha=r²`. Specular mip `m` represents
`r=m/(levels-1)`; mip zero samples the source directly. Other levels importance
sample GGX half-vectors with `N=V`, reject reflected directions below the
hemisphere, weight incoming radiance by `NoL`, then normalize accumulated
weights. The source sample footprint uses the GGX direction PDF, sample count
and local equirectangular texel solid angle. Its latitude term is bounded at
the polar cap; LOD is clamped to the source's actual levels.

Diffuse averages cosine-distributed incoming radiance, thus stores `E/pi`.
Consumers multiply it by diffuse albedo, **without another division by pi**.
BRDF UV is `(NoV,r)` at texel centers. The separate Fresnel basis integrates
`F0*A+B`. Environment Schlick visibility uses `k=r²/2`; the direct-light
hotness remap `(r+1)²/8` does not belong in this LUT.

For a constant environment, all reflection mips and diffuse outputs must retain
that constant. For `L(d)=1+.5*d` independently in R/G/B, diffuse is exactly
`1+n/3`; roughness-one specular has the same result. Roughness-zero is
`1+.5*n`. These references expose face orientation errors that constant light
cannot detect.

## Bounded work and ownership

`EnvironmentLightingConfig` permits power-of-two specular sizes 16–512,
diffuse 4–64, and BRDF 16–256; samples are 64–4096. There are at most twelve
dispatches. Each dispatch uses 8×8×1 workgroups, six array layers for a cube or
one for the LUT. No subgroup width or cross-workgroup synchronization is
assumed. Initialization owns exactly three textures plus their views, one
pipeline/layout/sampler and the bounded parameter buffers.

`init` stages CPU setup in a replacement owner. Missing handles, invalid
configuration, missing shader or failed host setup retain the previous owner.
WebGPU can return invalid non-null handles asynchronously: callers must wrap
creation and encoding in GPU error scopes before admitting/publishing results.
This helper deliberately does not label those results asynchronously validated.

`encodeBake` creates its bounded bind groups before opening the pass, then
encodes work without submitting. It has **no baked/completed flag**. The caller
must submit before sampling, track source revisions, re-encode after abandoning
an encoder, and distinguish encoded/submitted/completed state. Producer and
consumer in one ordered encoder are supported. Re-baking overwrites outputs;
the caller serializes readers/writers through queue order.

Views returned by `views()` are borrowed. Retain them in consumer bind groups
or explicit owner references. `releaseHandles` releases external references
without `Destroy`, allowing encoded commands and surviving bind groups to
keep resources alive. Requested byte count becoming zero means this owner
released its charge; it is not evidence of physical GPU retirement. The eventual
application owner must count this shared environment independently from per-asset
uploads and preserve queue/device-loss retirement rules.

## Next integration work

- Add an explicit filtered-lighting opt-in to MeshPath, with cube/cube/LUT
  bindings and a consistent CPU/WGSL uniform flag. Preserve legacy bindings and
  fallback behavior for original routes until separate comparisons pass.
- Retain all borrowed views in the salvage fixture owner and rebind active and
  candidate owners transactionally, under the existing GPU scopes. Guides can
  use their existing unlit path.
- Own the filter once per environment/device. Encode after original sky baking
  and before the first mesh consumer; do not multiply allocations per material.
  Track actual submission/revisions rather than marking completion at encoding.
- Use `prefiltered(R,r)*(F0*A+B)` for specular. Specify diffuse Fresnel/metal
  weighting explicitly and test normal/grazing angles. Clamp LUT UV at edges;
  never wrap `NoV` or roughness.
- Run native/browser material fixtures with identical geometry, textures,
  cameras and exposure. Include both normal-detail and factors-only ablation,
  varying light/exposure, all three detail levels and motion. Compare the
  original terrain/water route too.
- Keep final HDR composition, one output transform, moving shadows, hull
  exclusion, wet/glass and the owner-rejected cove open. A passing isolated
  producer is not a passed visual gate.

Future performance work must measure startup bake and full moving frames on
declared hardware. Current correctness timings include testing/readback/CPU
quadrature and do not establish game frame cost. Normal variance into material
roughness and narrow bright-environment convergence remain separate work.
