// ═══════════════════════════════════════════════════════════════════════════════
// terrain_raycast.wgsl - Compute Ray-Caster Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Performs hierarchical DDA ray-casting against the heightfield mip pyramid,
// outputting linear ray distance per pixel.
// Features:
//   - Ray generation from pixel coordinates using inverse view-projection
//   - AABB intersection for early ray clipping
//   - Hierarchical DDA traversal with mip level transitions
//   - Exact bilinear base-heightfield hit refinement
//   - Max-height mip pyramid for efficient empty-space skipping
//   - Shadow ray traversal
// ═══════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Uniforms
// ─────────────────────────────────────────────────────────────────────────────

struct CameraUniforms {
    viewProj : mat4x4<f32>,       // View-projection matrix
    invViewProj : mat4x4<f32>,    // Inverse view-projection
    invView : mat4x4<f32>,        // Inverse view matrix
    terrainSize : vec2<f32>,      // Heightmap dimensions (e.g., 8192, 8192)
    invTerrainSize : vec2<f32>,   // 1.0 / terrainSize
    metrics : vec4<f32>,          // (heightScale, cellScale, step, fogDensity)
    cameraPos : vec4<f32>,        // World-space camera position (.xyz)
    invProjParams : vec4<f32>,    // Inverse projection params (.xy used) - .z is Lego Mode
    lightDirVS : vec4<f32>,       // View-space light direction (.xyz)
    frustumPlanes : array<vec4<f32>, 6>, // Frustum planes
    lightDirWS : vec4<f32>,       // World-space light direction (.xyz)
    waterParams : vec4<f32>,      // (height, enabled, waveStrength, roughness)
    waterColorA : vec4<f32>,      // shallow color rgb, reflection strength
    waterColorB : vec4<f32>,      // deep color rgb, shore fade distance
    waterMotion : vec4<f32>,      // simulation time, reserved...
    lightingColor : vec4<f32>,
    ambientExposure : vec4<f32>,
    fogColor : vec4<f32>,
    waterOptics : vec4<f32>,
    waterFoam : vec4<f32>,
    waterSpectrum : vec4<f32>,
};

// ─────────────────────────────────────────────────────────────────────────────
// Bindings
// ─────────────────────────────────────────────────────────────────────────────

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var heightTex : texture_2d<u32>;
@group(0) @binding(2) var outDepth : texture_storage_2d<r32float, write>;
@group(0) @binding(3) var outShadow : texture_storage_2d<r32float, write>;
// Water-only auxiliary output: accepted normal XZ, compression, and shore.
@group(0) @binding(4) var outMaterial : texture_storage_2d<rgba16float, write>;
// Baked shadow boundary (see src/terrain/shadow_bake.hpp): per cell, the
// heightmap-space height below which a point is in the terrain's sun shadow.
// Baked at a fraction of the heightmap resolution.
@group(0) @binding(5) var shadowHeightTex : texture_2d<u32>;
@group(0) @binding(6) var waterDisplacementTex : texture_2d_array<f32>;
@group(0) @binding(7) var waterDisplacementSampler : sampler;
@group(0) @binding(8) var waterCoastFieldTex : texture_2d<f32>;

const MATERIAL_SKY : u32 = 0u;
const MATERIAL_TERRAIN : u32 = 1u;
const MATERIAL_WATER : u32 = 2u;
// Below this column depth the water is skipped and the bed shows through.
// Keep it small: at 2.0 every shore had a visible dry band below the
// waterline. Beer-Lambert extinction in the blit makes near-zero depths
// converge to the bed colour, so a low cutoff blends seamlessly.
const MIN_WATER_DEPTH : f32 = 0.05;
const SHORE_DEPTH : f32 = 7.5;
const SHORE_SURFACE_OVERLAP : f32 = 2.0;
const WATER_TAU : f32 = 6.283185307179586;
const WATER_RESOLUTION : f32 = 256.0;
const WATER_NORMAL_LAYER : i32 = 2;

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate Space Conversion
// ─────────────────────────────────────────────────────────────────────────────
// The ray-caster operates in heightmap coordinate space for Y comparisons
// (0-65535 range) to avoid per-sample normalization.

/// Convert world-space height to heightmap coordinate space
fn toHeightmapCoordinate(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5 + 0.5;
    return normalized * 65535.0;
}

/// Convert world-space height delta to heightmap scale
fn toHeightmapScale(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5;
    return normalized * 65535.0;
}

fn heightmapToWorldHeight(height : f32) -> f32 {
    return ((height / 65535.0) * 2.0 - 1.0) * camera.metrics.x;
}

fn terrainSurfaceNormal(
    worldPosition : vec3<f32>, terrainOrigin : vec2<f32>,
    cellScale : f32) -> vec3<f32> {
    let dimensions = vec2<i32>(
        i32(camera.terrainSize.x), i32(camera.terrainSize.y));
    let coordinate =
        (worldPosition.xz + terrainOrigin) / cellScale;
    let cell = clamp(
        vec2<i32>(floor(coordinate)),
        vec2<i32>(1), dimensions - vec2<i32>(3));
    let uv = clamp(
        coordinate - vec2<f32>(cell), vec2<f32>(0.0), vec2<f32>(1.0));
    let h00 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell, 0).x));
    let h10 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(1, 0), 0).x));
    let h01 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(0, 1), 0).x));
    let h11 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(1, 1), 0).x));

    // Bilinearly interpolate central differences at the four patch vertices.
    // This is the continuous shading normal of the sampled heightfield rather
    // than the visibly faceted derivative of one isolated one-metre patch.
    let hLeft0 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(-1, 0), 0).x));
    let hLeft1 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(-1, 1), 0).x));
    let hRight0 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(2, 0), 0).x));
    let hRight1 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(2, 1), 0).x));
    let hUp0 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(0, -1), 0).x));
    let hUp1 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(1, -1), 0).x));
    let hDown0 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(0, 2), 0).x));
    let hDown1 = heightmapToWorldHeight(
        f32(textureLoad(heightTex, cell + vec2<i32>(1, 2), 0).x));

    let dxNear = mix(
        0.5 * (h10 - hLeft0), 0.5 * (hRight0 - h00), uv.x);
    let dxFar = mix(
        0.5 * (h11 - hLeft1), 0.5 * (hRight1 - h01), uv.x);
    let dzNear = mix(
        0.5 * (h01 - hUp0), 0.5 * (h11 - hUp1), uv.x);
    let dzFar = mix(
        0.5 * (hDown0 - h00), 0.5 * (hDown1 - h10), uv.x);
    let heightDx = mix(dxNear, dxFar, uv.y) / cellScale;
    let heightDz = mix(dzNear, dzFar, uv.y) / cellScale;
    return normalize(vec3<f32>(-heightDx, 1.0, -heightDz));
}

/// Coarsest level that both exists in the texture and has at least one cell
/// on each terrain axis. This keeps traversal valid for small/non-square maps.
fn maxTraversalMip() -> u32 {
    let terrainWidth = max(u32(camera.terrainSize.x), 1u);
    let terrainHeight = max(u32(camera.terrainSize.y), 1u);
    var level = min(7u, textureNumLevels(heightTex) - 1u);
    loop {
        if (level == 0u ||
            ((terrainWidth >> level) > 0u && (terrainHeight >> level) > 0u)) {
            break;
        }
        level--;
    }
    return level;
}

fn rootInsideHeightCell(
    tau : f32, tEntry : f32, duration : f32, uvAtEntry : vec2<f32>,
    uvSlope : vec2<f32>) -> bool {
    let tolerance = 1e-3;
    let uv = uvAtEntry + uvSlope * tau;
    // Packed hierarchical descent can retain an entry time slightly ahead of
    // the selected child. The UV bounds are the authoritative cell test, so
    // permit an earlier root as long as it remains in front of the camera.
    return tEntry + tau >= tolerance && tau <= duration + tolerance
        && all(uv >= vec2<f32>(-tolerance))
        && all(uv <= vec2<f32>(1.0 + tolerance));
}

// Intersect one continuous bilinear base-heightfield patch. Hierarchical mips
// are conservative acceleration data only; accepting their maximum height as
// geometry creates the visible square columns and moving contour bands which
// plagued the old distance-LOD path.
fn intersectBilinearHeightCell(
    origin : vec3<f32>, dir : vec3<f32>, terrainOrigin : vec2<f32>,
    cellScale : f32, cell : vec2<i32>, tEntry : f32, tExit : f32,
    originY : f32, slopeY : f32) -> f32 {
    let h00 = f32(textureLoad(heightTex, cell, 0).x) - originY;
    let h10 = f32(textureLoad(
        heightTex, cell + vec2<i32>(1, 0), 0).x) - originY;
    let h01 = f32(textureLoad(
        heightTex, cell + vec2<i32>(0, 1), 0).x) - originY;
    let h11 = f32(textureLoad(
        heightTex, cell + vec2<i32>(1, 1), 0).x) - originY;

    let duration = max(tExit - tEntry, 0.0);
    let entryPosition = origin.xz + dir.xz * tEntry;
    let uvAtEntry =
        (entryPosition + terrainOrigin) / cellScale - vec2<f32>(cell);
    let uvSlope = dir.xz / cellScale;

    let hx = h10 - h00;
    let hz = h01 - h00;
    let hxz = h11 - h10 - h01 + h00;
    let heightConstant = h00 + hx * uvAtEntry.x + hz * uvAtEntry.y
        + hxz * uvAtEntry.x * uvAtEntry.y;
    let heightLinear = hx * uvSlope.x + hz * uvSlope.y
        + hxz * (uvAtEntry.x * uvSlope.y
               + uvAtEntry.y * uvSlope.x);
    let heightQuadratic = hxz * uvSlope.x * uvSlope.y;

    // rayY(tEntry + tau) - bilinearHeight(tEntry + tau) = 0.
    let a = -heightQuadratic;
    let b = slopeY - heightLinear;
    let c = slopeY * tEntry - heightConstant;
    var best = -1.0;
    let coefficientEpsilon = 1e-6;

    if (abs(a) <= coefficientEpsilon) {
        if (abs(b) > coefficientEpsilon) {
            let tau = -c / b;
            if (rootInsideHeightCell(
                    tau, tEntry, duration, uvAtEntry, uvSlope)) {
                best = tEntry + min(tau, duration);
            }
        } else if (abs(c) <= 1e-3
                   && rootInsideHeightCell(
                       0.0, tEntry, duration, uvAtEntry, uvSlope)) {
            best = tEntry;
        }
    } else {
        let discriminant = b * b - 4.0 * a * c;
        if (discriminant >= 0.0) {
            let squareRoot = sqrt(discriminant);
            let q = -0.5 * (b + select(-squareRoot, squareRoot, b >= 0.0));
            var first = -b / (2.0 * a);
            var second = first;
            if (abs(q) > coefficientEpsilon) {
                first = q / a;
                second = c / q;
            }
            let low = min(first, second);
            let high = max(first, second);
            if (rootInsideHeightCell(
                    low, tEntry, duration, uvAtEntry, uvSlope)) {
                best = tEntry + min(low, duration);
            } else if (rootInsideHeightCell(
                           high, tEntry, duration, uvAtEntry, uvSlope)) {
                best = tEntry + min(high, duration);
            }
        }
    }
    return best;
}

struct WaterSurfaceSample {
    height : f32,
    geometrySlope : vec2<f32>,
    shadingWave : vec4<f32>,
};

struct WaterRayHit {
    distance : f32,
    surface : WaterSurfaceSample,
};

struct SpectralSurface {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
    compression : f32,
};

struct LongWaveSurface {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
    fold : f32,
};

fn cascadeUv(position : vec2<f32>, scale : f32) -> vec2<f32> {
    return position / scale +
           vec2<f32>(0.5 + 0.5 / WATER_RESOLUTION);
}

fn spectralSurface(worldXZ : vec2<f32>, strength : f32,
                   distance : f32) -> SpectralSurface {
    // The mesh implementation naturally loses sub-pixel vertices in its far
    // rings. A ray surface needs the equivalent explicit band limit or the
    // 1.27 m detail cells alias into a picket fence at the horizon.
    let detailWeight = 1.0 - smoothstep(900.0, 3500.0, max(distance, 0.0));
    let broadFirst = textureSampleLevel(
        waterDisplacementTex, waterDisplacementSampler,
        cascadeUv(worldXZ, camera.waterSpectrum.x), 0, 0.0);
    var detailFirst = vec4<f32>(0.0);
    if (detailWeight > 0.0) {
        detailFirst = textureSampleLevel(
            waterDisplacementTex, waterDisplacementSampler,
            cascadeUv(worldXZ, camera.waterSpectrum.y), 1, 0.0) * detailWeight;
    }
    // Horizontal displacement changes which base-grid point reaches this world
    // position. One inverse step captures the sharp crest compression without
    // requiring a mesh or an iterative per-pixel solve.
    let baseXZ = worldXZ - (broadFirst.xz + detailFirst.xz) * strength;
    let broad = textureSampleLevel(
        waterDisplacementTex, waterDisplacementSampler,
        cascadeUv(baseXZ, camera.waterSpectrum.x), 0, 0.0);
    let broadNormal = textureSampleLevel(
        waterDisplacementTex, waterDisplacementSampler,
        cascadeUv(baseXZ, camera.waterSpectrum.x), WATER_NORMAL_LAYER, 0.0).xyz;
    var detail = vec4<f32>(0.0);
    var detailNormal = vec3<f32>(0.0, 1.0, 0.0);
    if (detailWeight > 0.0) {
        detail = textureSampleLevel(
            waterDisplacementTex, waterDisplacementSampler,
            cascadeUv(baseXZ, camera.waterSpectrum.y), 1, 0.0) * detailWeight;
        detailNormal = textureSampleLevel(
            waterDisplacementTex, waterDisplacementSampler,
            cascadeUv(baseXZ, camera.waterSpectrum.y),
            WATER_NORMAL_LAYER + 1, 0.0).xyz;
    }
    let up = vec3<f32>(0.0, 1.0, 0.0);
    return SpectralSurface(
        (broad.xyz + detail.xyz) * strength,
        up + ((broadNormal - up) +
              (detailNormal - up) * detailWeight) * strength,
        max(broad.w, detail.w) * strength);
}

fn oneLongWave(position : vec2<f32>, wave : vec4<f32>,
               dynamics : vec3<f32>, strength : f32) -> LongWaveSurface {
    let waveNumber = WATER_TAU / (wave.w + 1.0e-4);
    let phase = waveNumber * dot(wave.xy, position) -
                dynamics.z * camera.waterMotion.x + dynamics.y;
    let sine = sin(phase);
    let cosine = cos(phase);
    let amplitude = wave.z * strength;
    let ka = waveNumber * amplitude;
    return LongWaveSurface(
        vec3<f32>(-dynamics.x * amplitude * wave.x * sine,
                  amplitude * cosine,
                  -dynamics.x * amplitude * wave.y * sine),
        vec3<f32>(wave.x * ka * sine,
                  -dynamics.x * ka * cosine,
                  wave.y * ka * sine),
        dynamics.x * ka * sine);
}

fn longWaveSurface(position : vec2<f32>, strength : f32) -> LongWaveSurface {
    let a = oneLongWave(
        position, vec4<f32>(0.923059017, 0.384658357, 5.1541, 440.298507),
        vec3<f32>(1.0, 0.000000000, 0.374291312), strength);
    let b = oneLongWave(
        position, vec4<f32>(0.700400636, 0.713749921, 5.1541, 701.258144),
        vec3<f32>(1.0, 5.553108549, 0.296825282), strength);
    let c = oneLongWave(
        position, vec4<f32>(0.367164395, 0.930156066, 5.1541, 1116.885424),
        vec3<f32>(1.0, 4.823031791, 0.234699061), strength);
    let d = oneLongWave(
        position, vec4<f32>(-0.024039031, 0.999711021, 5.1541, 1778.85),
        vec3<f32>(1.0, 4.092955033, 0.186378666), strength);
    return LongWaveSurface(
        a.displacement + b.displacement + c.displacement + d.displacement,
        vec3<f32>(0.0, 1.0, 0.0) +
            a.normalVector + b.normalVector + c.normalVector + d.normalVector,
        a.fold + b.fold + c.fold + d.fold);
}

fn sampleWaterSurface(worldXZ : vec2<f32>, distance : f32) -> WaterSurfaceSample {
    // The horizon filter is identically zero beyond this point. Skipping the
    // cascade fetches and long waves preserves the same image at lower cost.
    if (distance >= 6500.0) {
        return WaterSurfaceSample(
            0.0, vec2<f32>(0.0), vec4<f32>(0.0));
    }
    let strength = clamp(camera.waterParams.z, 0.0, 2.0);
    let spectral = spectralSurface(worldXZ, strength, distance);
    let longWaves = longWaveSurface(
        worldXZ - spectral.displacement.xz, strength);
    var normal = spectral.normalVector +
                 longWaves.normalVector - vec3<f32>(0.0, 1.0, 0.0);
    if (dot(normal, normal) > 1.0e-8) {
        normal = normalize(normal);
    } else {
        normal = vec3<f32>(0.0, 1.0, 0.0);
    }
    let normalY = max(normal.y, 0.08);
    let geometrySlope = -normal.xz / normalY;
    let displacement = spectral.displacement + longWaves.displacement;
    let compression = max(spectral.compression,
                          clamp(longWaves.fold, 0.0, 2.0));
    return WaterSurfaceSample(
        displacement.y, geometrySlope,
        vec4<f32>(displacement.y, normal.x, normal.z, compression));
}

fn filterWaterSurfaceForHorizon(surfaceIn : WaterSurfaceSample,
                                distance : f32) -> WaterSurfaceSample {
    var surface = surfaceIn;
    // This is the ray equivalent of the progressively coarser far mesh: keep
    // full geometry nearby, then remove sub-pixel displacement before grazing
    // rays can jump between several wave roots.
    let detail = 1.0 - smoothstep(1800.0, 6500.0, max(distance, 0.0));
    surface.height *= detail;
    surface.geometrySlope *= detail;
    surface.shadingWave = vec4<f32>(
        surface.shadingWave.x * detail,
        surface.shadingWave.yz * detail,
        surface.shadingWave.w * detail);
    return surface;
}

fn intersectWaterSurface(origin : vec3<f32>, dir : vec3<f32>,
                         waterHeight : f32) -> WaterRayHit {
    // Bound Newton updates by the complete vertical wave envelope. Without
    // this bracket, a nearly tangent ray can jump behind the camera and leave
    // a sky hole in an otherwise continuous ocean.
    let maximumWaveHeight = 48.0 * clamp(camera.waterParams.z, 0.0, 2.0);
    let envelopeA =
        (waterHeight - maximumWaveHeight - origin.y) / dir.y;
    let envelopeB =
        (waterHeight + maximumWaveHeight - origin.y) / dir.y;
    let minimumDistance = max(min(envelopeA, envelopeB), 1.0e-3);
    let maximumDistance = max(max(envelopeA, envelopeB), minimumDistance);
    var distance = (waterHeight - origin.y) / dir.y;
    distance = clamp(distance, minimumDistance, maximumDistance);
    var position = origin + dir * distance;
    var surface = filterWaterSurfaceForHorizon(
        sampleWaterSurface(position.xz, distance), distance);
    var residual = position.y - waterHeight - surface.height;
    var derivative = dir.y - dot(surface.geometrySlope, dir.xz);
    if (abs(derivative) > 1.0e-4) {
        let maximumStep = 32.0 / max(abs(dir.y), 0.002);
        distance = clamp(
            distance - clamp(residual / derivative,
                             -maximumStep, maximumStep),
            minimumDistance, maximumDistance);
    }

    // One damped correction preserves the displaced silhouette without
    // turning a near-horizontal ray into an unbounded iteration.
    if (abs(dir.y) < 0.065) {
        position = origin + dir * distance;
        surface = filterWaterSurfaceForHorizon(
            sampleWaterSurface(position.xz, distance), distance);
        residual = position.y - waterHeight - surface.height;
        derivative = dir.y - dot(surface.geometrySlope, dir.xz);
        if (abs(derivative) > 1.0e-4) {
            let maximumStep = 16.0 / max(abs(dir.y), 0.002);
            distance = clamp(
                distance - clamp(residual / derivative,
                                 -maximumStep, maximumStep),
                minimumDistance, maximumDistance);
        }
    }

    surface.height = origin.y + dir.y * distance - waterHeight;
    return WaterRayHit(distance, surface);
}

fn nearbyShoreInfluence(cell : vec2<i32>, baseSize : vec2<i32>, waterHeight : f32) -> f32 {
    // The max-height mip pyramid already stores neighborhood maxima: 2x2
    // taps at mip 3 (8-cell blocks, centered on the query cell) cover the
    // same ~11-cell footprint the old version probed with 16 scattered
    // mip-0 loads.
    let shoreMip = min(3u, maxTraversalMip());
    let blockSize = 1u << shoreMip;
    let levelSize = max(
        vec2<i32>(baseSize.x >> shoreMip, baseSize.y >> shoreMip),
        vec2<i32>(1, 1));
    let base = vec2<i32>(floor(
        vec2<f32>(cell) / f32(blockSize) - vec2<f32>(0.5, 0.5)));

    var maxNearbyHeight = -1.0e20;
    for (var dz = 0; dz < 2; dz++) {
        for (var dx = 0; dx < 2; dx++) {
            let c = clamp(base + vec2<i32>(dx, dz), vec2<i32>(0, 0), levelSize - vec2<i32>(1, 1));
            let h = heightmapToWorldHeight(
                f32(textureLoad(heightTex, c, i32(shoreMip)).x));
            maxNearbyHeight = max(maxNearbyHeight, h);
        }
    }

    return smoothstep(waterHeight - 36.0, waterHeight - MIN_WATER_DEPTH, maxNearbyHeight);
}

// ─────────────────────────────────────────────────────────────────────────────
// AABB Intersection
// ─────────────────────────────────────────────────────────────────────────────

/// Ray-AABB intersection returning (tMin, tMax)
/// Returns (tMin, tMax) where tMin > tMax indicates no intersection
fn intersectAabb(origin : vec3<f32>, dir : vec3<f32>, 
                 bmin : vec3<f32>, bmax : vec3<f32>) -> vec2<f32> {
    // Avoid division by zero for axis-aligned rays by adding a tiny epsilon
    let invDir = 1.0 / (dir + sign(dir) * 1e-20 + vec3<f32>(1e-20)); 

    let t0 = (bmin - origin) * invDir;
    let t1 = (bmax - origin) * invDir;
    let tMin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    let tMax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return vec2<f32>(tMin, tMax);
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray Generation
// ─────────────────────────────────────────────────────────────────────────────

/// Generate a world-space ray direction from projection scale and camera rotation.
/// This avoids a full inverse-VP transform, divide, and camera subtraction per pixel.
fn rayDirFromPixel(pixel : vec2<u32>, dims : vec2<u32>) -> vec3<f32> {
    let dimf = vec2<f32>(f32(dims.x), f32(dims.y));
    let pixelF = vec2<f32>(f32(pixel.x), f32(pixel.y));
    let uv = (pixelF + vec2<f32>(0.5, 0.5)) / dimf;
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let viewDir = vec3<f32>(ndc * camera.invProjParams.xy, 1.0);
    return normalize((camera.invView * vec4<f32>(viewDir, 0.0)).xyz);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lego Logic
// ─────────────────────────────────────────────────────────────────────────────

fn intersectStud(origin: vec3<f32>, dir: vec3<f32>,
                 studBase: vec3<f32>, studH: f32, studR: f32) -> f32 {
    // 1. Intersect with cylinder cap (top) at y = studBase.y + studH
    // Avoid divide by zero for horizontal rays
    var tHit = 1e30;
    if (abs(dir.y) > 1e-6) {
        let tCap = (studBase.y + studH - origin.y) / dir.y;
        if (tCap > 1e-3) {
            let pCap = origin + dir * tCap;
            let dx = pCap.x - studBase.x;
            let dz = pCap.z - studBase.z;
            if (dx*dx + dz*dz <= studR*studR) {
                tHit = tCap;
            }
        }
    }

    // 2. Intersect with cylinder wall
    // (O.x + D.x*t - C.x)^2 + (O.z + D.z*t - C.z)^2 = R^2
    let ocX = origin.x - studBase.x;
    let ocZ = origin.z - studBase.z;
    let A = dir.x*dir.x + dir.z*dir.z;

    // Safety check for vertical rays
    if (A > 1e-6) {
        let B = 2.0 * (ocX*dir.x + ocZ*dir.z);
        let C = ocX*ocX + ocZ*ocZ - studR*studR;
        let disc = B*B - 4.0*A*C;

        if (disc >= 0.0) {
            let sqrtDisc = sqrt(disc);
            let t1 = (-B - sqrtDisc) / (2.0*A);
            let t2 = (-B + sqrtDisc) / (2.0*A);

            var tCyl = -1.0;
            if (t1 > 1e-3) { tCyl = t1; }
            else if (t2 > 1e-3 && (tCyl < 0.0 || t2 < tCyl)) { tCyl = t2; }

            if (tCyl > 1e-3) {
                let pCyl = origin + dir * tCyl;
                // Check height bounds
                if (pCyl.y >= studBase.y && pCyl.y <= studBase.y + studH) {
                    if (tCyl < tHit) {
                        tHit = tCyl;
                    }
                }
            }
        }
    }

    if (tHit < 1e29) { return tHit; }
    return -1.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Baked Shadow Lookup
// ─────────────────────────────────────────────────────────────────────────────

/// Sun visibility from the baked shadow height field: one load + one compare.
/// The boundary excludes the receiving cell's own terrain, so no bias is
/// needed. The smooth band hides the reduced bake resolution and gives the
/// shadow edge a soft penumbra for free.
fn sampleBakedShadow(worldPos : vec3<f32>, terrainOrigin : vec2<f32>, cellScale : f32) -> f32 {
    let dims = vec2<i32>(textureDimensions(shadowHeightTex));
    let cellF = (worldPos.xz + terrainOrigin) / cellScale;
    let scale = vec2<f32>(dims) / camera.terrainSize;
    let cell = clamp(vec2<i32>(floor(cellF * scale)), vec2<i32>(0, 0), dims - vec2<i32>(1, 1));
    let boundary = f32(textureLoad(shadowHeightTex, cell, 0).x);
    let posY = toHeightmapCoordinate(worldPos.y);
    let soft = max(toHeightmapScale(1.5), 1.0);
    return smoothstep(-soft, soft, posY - boundary);
}

/// Lego terrain still uses the baked terrain shadow. Only the short shadows
/// cast by studs need extra work. A stud is 0.2 cells tall, so the exact
/// sunward footprint normally covers four cells (and at most a small bounded
/// rectangle for low sun angles). This replaces a second hierarchical DDA per
/// hit pixel with a handful of coherent texture loads and analytic cylinders.
fn sampleLegoShadow(worldPos : vec3<f32>, lightDir : vec3<f32>,
                    terrainOrigin : vec2<f32>, cellScale : f32,
                    hitDistance : f32) -> f32 {
    let terrainShadow = sampleBakedShadow(
        worldPos, terrainOrigin, cellScale);
    // Beyond this distance a stud is smaller than a pixel in the intended
    // Lego views. Keep the baked brick shadow and skip sub-pixel cylinders.
    if (terrainShadow <= 0.001 || lightDir.y <= 1e-4
        || hitDistance > cellScale * 64.0) {
        return terrainShadow;
    }

    let studHeight = cellScale * 0.2;
    let studRadius = cellScale * 0.35;
    let footprint = vec2<f32>(studRadius)
        + abs(lightDir.xz) * (studHeight / lightDir.y);
    let steps = clamp(vec2<i32>(ceil(footprint / cellScale)),
                      vec2<i32>(1), vec2<i32>(4));
    let direction = vec2<i32>(select(-1, 1, lightDir.x >= 0.0),
                              select(-1, 1, lightDir.z >= 0.0));
    let baseCell = vec2<i32>(floor(
        (worldPos.xz + terrainOrigin) / cellScale));
    let terrainSize = vec2<i32>(camera.terrainSize);
    let shadowOrigin = worldPos + lightDir * studHeight;

    for (var x = 0; x <= steps.x; x += 1) {
        for (var z = 0; z <= steps.y; z += 1) {
            let cell = baseCell + vec2<i32>(x * direction.x,
                                            z * direction.y);
            if (any(cell < vec2<i32>(0)) || any(cell >= terrainSize)) {
                continue;
            }
            let height = f32(textureLoad(heightTex, cell, 0).x);
            let brickY = heightmapToWorldHeight(height);
            let centerXZ = (vec2<f32>(cell) + vec2<f32>(0.5))
                * cellScale - terrainOrigin;
            let studBase = vec3<f32>(centerXZ.x, brickY, centerXZ.y);
            if (intersectStud(shadowOrigin, lightDir, studBase,
                              studHeight, studRadius) > 0.0) {
                return 0.0;
            }
        }
    }
    return terrainShadow;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Ray Traversal (Lego mode only — studs are not in the baked field)
// ─────────────────────────────────────────────────────────────────────────────

/// Coarsest mip the shadow march may resolve to, scaled by distance along
/// the ray. Shadow detail matters most near the receiver; far away, a
/// coarse max-height cell is accepted as an occluder.
fn shadowMinMip(t : f32, cellScale : f32) -> u32 {
    let cells = t / cellScale;
    if (cells < 96.0) { return 0u; }
    if (cells < 256.0) { return 1u; }
    if (cells < 640.0) { return 2u; }
    return 3u;
}

/// Optimized shadow ray traversal
/// Returns 0.0 if shadowed, 1.0 if lit
fn intersectShadow(origin : vec3<f32>, dir : vec3<f32>) -> f32 {
    let legoMode = camera.invProjParams.z > 0.5;

    // Basic terrain setup (reused from main)
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellScale = camera.metrics.y;
    let terrainOrigin = 0.5 * (terrainSize - vec2<f32>(1.0, 1.0)) * cellScale;
    let borderMargin = cellScale * 1.0;
    let boundsMin = vec3<f32>(
        -terrainOrigin.x + borderMargin,
        -camera.metrics.x,
        -terrainOrigin.y + borderMargin
    );
    let boundsMax = vec3<f32>(
        terrainSize.x * cellScale - terrainOrigin.x - borderMargin,
        camera.metrics.x,
        terrainSize.y * cellScale - terrainOrigin.y - borderMargin
    );

    // Intersect AABB
    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        return 1.0; // Lit (escapes bounds)
    }

    var t = max(range.x, 0.0) + 1e-4;

    // Check if we are already out of bounds
    if (t > range.y) { return 1.0; }

    // Start fine near the biased surface, then ascend aggressively in open space.
    // Canyon walls make a coarse starting cell overly conservative and can cause
    // repeated descent through unrelated height maxima.
    let maxMipLevel = maxTraversalMip();
    var mipLevel : u32 = 0u;

    // Standard DDA Setup (packed coordinates: Sebbbi's approach)
    var pos = origin + dir * t;
    let cellScaleMip = cellScale * f32(1u << mipLevel);
    let sizeI = vec2<i32>(i32(camera.terrainSize.x) >> mipLevel,
                          i32(camera.terrainSize.y) >> mipLevel);

    let cellX_init = clamp(i32(floor((pos.x + terrainOrigin.x) / cellScaleMip)), 0, sizeI.x - 1);
    let cellZ_init = clamp(i32(floor((pos.z + terrainOrigin.y) / cellScaleMip)), 0, sizeI.y - 1);
    var cellPacked = (cellZ_init << 16) | cellX_init;

    let stepX = select(-1, 1, dir.x >= 0.0);
    let stepZ = i32(select(-1, 1, dir.z >= 0.0)) << 16;
    let offsetX = select(0, 1, dir.x >= 0.0);
    let offsetZ = select(0, 1, dir.z >= 0.0) << 16;
    let offsetPacked = offsetZ | offsetX;

    let nextBoundaryX = (f32(cellX_init + offsetX) * cellScaleMip) - terrainOrigin.x;
    let nextBoundaryZ = (f32(cellZ_init + (offsetZ >> 16)) * cellScaleMip) - terrainOrigin.y;
    // Guard against near-zero (subnormal) direction components: an exact != 0.0
    // test lets tiny values through and the division overflows to inf/NaN.
    let ddaEpsilon = 1e-6;
    var tMaxX = select(1e30, (nextBoundaryX - pos.x) / dir.x + t, abs(dir.x) > ddaEpsilon);
    var tMaxZ = select(1e30, (nextBoundaryZ - pos.z) / dir.z + t, abs(dir.z) > ddaEpsilon);
    var tDeltaX = select(1e30, cellScaleMip / abs(dir.x), abs(dir.x) > ddaEpsilon);
    var tDeltaZ = select(1e30, cellScaleMip / abs(dir.z), abs(dir.z) > ddaEpsilon);

    let slopeY = toHeightmapScale(dir.y);
    let originY = toHeightmapCoordinate(origin.y);

    var loopCount = 0u;

    // Loop Limit: Increased for mip 0 traversal
    loop {
        loopCount++;
        if (loopCount > 2000u) { break; } // Safety break

        var tNext = min(tMaxX, tMaxZ);
        var yEnter = slopeY * t;
        var yExit = slopeY * tNext;

        // Sample height (unpack coordinates)
        var h = -1.0e30;
        let levelW = i32(camera.terrainSize.x) >> mipLevel;
        let levelH = i32(camera.terrainSize.y) >> mipLevel;
        let cellX = cellPacked & 0xffff;
        let cellZ = cellPacked >> 16;

        if (cellX >= 0 && cellX < levelW && cellZ >= 0 && cellZ < levelH) {
             h = f32(textureLoad(heightTex, vec2<i32>(cellX, cellZ), i32(mipLevel)).x) - originY;
        }

        // Intersection Check (Any Hit)
        // If ray enters below terrain height, it's blocked.
        // For shadows, we are strict: if any part of the segment is below height, shadow.
        // However, since we traverse empty space, we only care if the ray *starts* below,
        // or enters below.

        // Lego Mode: Studs protrude above h. We need to check against h + studHeight
        // to avoid skipping cells where the ray hits the stud but is above the brick surface.
        // Stud is ~0.2 units high.
        let hCheck = select(h, h + toHeightmapScale(cellScale * 0.25), legoMode && mipLevel == 0u);

        if (min(yEnter, yExit) <= hCheck) {
             // Potential hit - check if we need to descend or confirm hit
             
             if (mipLevel == 0u) {
                 if (legoMode) {
                     // 1. Calculate absolute World Y of the brick surface
                     // h is relative to originY in 0-65535 space
                     let h_map = (h + originY) / 65535.0;
                     let brickY = (h_map * 2.0 - 1.0) * camera.metrics.x;

                     // 2. Check Stud Intersection
                     let studHeightWorld = cellScale * 0.2;
                     let studRadiusWorld = cellScale * 0.35;
                     let cellCenterXZ = (vec2<f32>(f32(cellX)+0.5, f32(cellZ)+0.5)) * cellScale - terrainOrigin;
                     let studBasePos = vec3<f32>(cellCenterXZ.x, brickY, cellCenterXZ.y);

                     let tStud = intersectStud(origin, dir, studBasePos, studHeightWorld, studRadiusWorld);
                     if (tStud > 0.0 && tStud < tNext) {
                         return 0.0; // Hit stud
                     }

                     // 3. Check Brick Body Intersection
                     // Brick is defined by y <= brickY within this cell.
                     // The ray is in this cell between t and tNext.
                     // We just need to know if the ray segment [t, tNext] overlaps y <= brickY.
                     // yEnter = relative height at t, yExit = relative height at tNext
                     // brick height relative = h
                     if (min(yEnter, yExit) <= h) {
                         return 0.0; // Hit brick
                     }

                     // If we are here, we hit the expanded bounds (hCheck) but missed stud and brick.
                     // Fall through to "Next Cell".
                 } else {
                    return 0.0; // Hit standard terrain
                 }
             } else {
                 // Far from the receiver, accept the coarse cell as a blocker
                 // instead of descending all the way to mip 0.
                 if (mipLevel <= shadowMinMip(t, cellScale)) { return 0.0; }
                 // Descend to Finer Mip Level (Sebbbi's packed approach)
                 mipLevel--;
                 tDeltaX *= 0.5;
                 tDeltaZ *= 0.5;
                 cellPacked = (cellPacked << 1) + offsetPacked;

                 // Relative boundary adjustment
                 if (t < tMaxX - tDeltaX) {
                     tMaxX -= tDeltaX;
                     cellPacked -= stepX;
                 }
                 if (t < tMaxZ - tDeltaZ) {
                     tMaxZ -= tDeltaZ;
                     cellPacked -= stepZ;
                 }
                 continue;
             }
        }

        // Next cell
        t = tNext;
        if (t > range.y) { break; } // Escaped terrain

        if (tMaxX < tMaxZ) {
            tMaxX += tDeltaX;
            cellPacked += stepX;
        } else {
            tMaxZ += tDeltaZ;
            cellPacked += stepZ;
        }

        // Level-Up Check - Ascend to Coarser Mip
        // When ray is far above terrain surface (relative to current mip height), ascend
        // Use stricter threshold for shadows to avoid missing thin occluders?
        // Use standard threshold from main loop
        let levelUpHeight = f32(128u << mipLevel);
        if (mipLevel < maxMipLevel && yExit - levelUpHeight > h) {
            mipLevel++;
            if ((cellPacked & 1) != offsetX) { tMaxX += tDeltaX; }
            if ((cellPacked & 65536) != offsetZ) { tMaxZ += tDeltaZ; }
            tDeltaX *= 2.0;
            tDeltaZ *= 2.0;
            cellPacked = i32((u32(cellPacked) & 0xfffefffe) >> 1);
        }
    }

    return 1.0; // Lit
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────
// Hierarchical DDA traversal algorithm:
//   1. Start at coarse mip level (7 for 8192×8192 = 64×64 cells)
//   2. Sample max-height at current cell and mip level
//   3. If ray potentially intersects cell's max height:
//      a. Descend to the full-resolution cell
//      b. Solve its continuous bilinear patch exactly
//   4. If ray clearly misses, step to next cell
//   5. Ascend to coarser mip when far from terrain surface (level-up)

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(outDepth);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }
    
    let origin = camera.cameraPos.xyz;
    let dir = rayDirFromPixel(gid.xy, dims);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Terrain Bounds
    // ─────────────────────────────────────────────────────────────────────────
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellScale = camera.metrics.y;
    let terrainOrigin = 0.5 * (terrainSize - vec2<f32>(1.0, 1.0)) * cellScale;
    let borderMargin = cellScale * 1.0;
    let boundsMin = vec3<f32>(
        -terrainOrigin.x + borderMargin,
        -camera.metrics.x,
        -terrainOrigin.y + borderMargin
    );
    let boundsMax = vec3<f32>(
        terrainSize.x * cellScale - terrainOrigin.x - borderMargin,
        camera.metrics.x,
        terrainSize.y * cellScale - terrainOrigin.y - borderMargin
    );
    
    // ─────────────────────────────────────────────────────────────────────────
    // AABB Intersection
    // ─────────────────────────────────────────────────────────────────────────
    let waterEnabled = camera.waterParams.y > 0.5;
    let waterHeight = camera.waterParams.x;
    var waterHit = WaterRayHit(
        -1.0, WaterSurfaceSample(0.0, vec2<f32>(0.0), vec4<f32>(0.0)));
    if (waterEnabled && abs(dir.y) > 1e-5) {
        let flatWaterDistance = (waterHeight - origin.y) / dir.y;
        if (flatWaterDistance > 0.0 && flatWaterDistance < 50000.0) {
            waterHit = intersectWaterSurface(origin, dir, waterHeight);
        }
    }

    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        // The heightfield is finite, but the open ocean is not. Rays that miss
        // terrain still intersect the same displaced surface over deep water.
        if (waterHit.distance > 0.0 && waterHit.distance < 50000.0) {
            textureStore(outDepth, vec2<i32>(gid.xy),
                         vec4<f32>(waterHit.distance, 0.0, 0.0, 0.0));
            textureStore(outShadow, vec2<i32>(gid.xy),
                         vec4<f32>(501.0, 0.0, 0.0, 0.0));
            textureStore(outMaterial, vec2<i32>(gid.xy),
                         vec4<f32>(waterHit.surface.shadingWave.y,
                                   waterHit.surface.shadingWave.z,
                                   waterHit.surface.shadingWave.w, 0.0));
            return;
        }
        textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(-1.0, 0.0, 0.0, 0.0));
        return;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Initialize DDA at the coarsest valid traversal level (at most 7).
    // ─────────────────────────────────────────────────────────────────────────
    var t = max(range.x, 0.0) + 1e-4;
    let maxMipLevel = maxTraversalMip();
    var mipLevel : u32 = maxMipLevel;
    var pos = origin + dir * t;
    
    let cellScaleMip = cellScale * f32(1u << mipLevel);
    let sizeI = vec2<i32>(i32(camera.terrainSize.x) >> mipLevel, 
                          i32(camera.terrainSize.y) >> mipLevel);
    
    // Initialize cell position (packed: X in low 16 bits, Z in high 16 bits)
    let cellX_init = clamp(i32(floor((pos.x + terrainOrigin.x) / cellScaleMip)), 0, sizeI.x - 1);
    let cellZ_init = clamp(i32(floor((pos.z + terrainOrigin.y) / cellScaleMip)), 0, sizeI.y - 1);
    var cellPacked = (cellZ_init << 16) | cellX_init;
    
    // DDA Setup (packed coordinates: Sebbbi's approach)
    // ─────────────────────────────────────────────────────────────────────────
    let stepX = select(-1, 1, dir.x >= 0.0);
    let stepZ = i32(select(-1, 1, dir.z >= 0.0)) << 16;  // Packed Z step in high bits
    let offsetX = select(0, 1, dir.x >= 0.0);
    let offsetZ = select(0, 1, dir.z >= 0.0) << 16;       // Packed Z offset in high bits
    let offsetPacked = offsetZ | offsetX;
    // Initialize tMax (time to next boundary) and tDelta (time to cross one cell)
    let nextBoundaryX = (f32(cellX_init + offsetX) * cellScaleMip) - terrainOrigin.x;
    let nextBoundaryZ = (f32(cellZ_init + (offsetZ >> 16)) * cellScaleMip) - terrainOrigin.y;
    // Guard against near-zero (subnormal) direction components: an exact != 0.0
    // test lets tiny values through and the division overflows to inf/NaN.
    let ddaEpsilon = 1e-6;
    var tMaxX = select(1e30, (nextBoundaryX - pos.x) / dir.x + t, abs(dir.x) > ddaEpsilon);
    var tMaxZ = select(1e30, (nextBoundaryZ - pos.z) / dir.z + t, abs(dir.z) > ddaEpsilon);
    var tDeltaX = select(1e30, cellScaleMip / abs(dir.x), abs(dir.x) > ddaEpsilon);
    var tDeltaZ = select(1e30, cellScaleMip / abs(dir.z), abs(dir.z) > ddaEpsilon);

    // Convert ray Y to heightmap coordinate space for efficient comparisons
    let slopeY = toHeightmapScale(dir.y);
    let originY = toHeightmapCoordinate(origin.y);
    
    // ─────────────────────────────────────────────────────────────────────────
    // DDA Loop - Hierarchical Traversal
    // ─────────────────────────────────────────────────────────────────────────
    let legoMode = camera.invProjParams.z > 0.5;

    var loopCount = 0u;
    loop {
        loopCount++;
        if (loopCount > 2000u) {
            t = -2.0;
            break;
        }

        var tNext = min(tMaxX, tMaxZ);
        
        // Compute ray Y in heightmap space at entry and exit of current segment
        var yEnter = slopeY * t;
        var yExit = slopeY * tNext;
        
        // Sample max height from mip pyramid (already in heightmap space 0-65535)
        // Subtract originY to make comparison relative to ray origin
        // Unpack coordinates: X = low 16 bits, Z = high 16 bits
        var h = -1.0e30;
        let levelW = i32(camera.terrainSize.x) >> mipLevel;
        let levelH = i32(camera.terrainSize.y) >> mipLevel;
        let cellX = cellPacked & 0xffff;
        let cellZ = cellPacked >> 16;

        if (cellX >= 0 && cellX < levelW && cellZ >= 0 && cellZ < levelH) {
            h = f32(textureLoad(heightTex, vec2<i32>(cellX, cellZ), i32(mipLevel)).x) - originY;
            // Level zero is the raw vertex field, so a cell's conservative
            // height uses all four corners. Coarser levels were generated with
            // overlapping shared boundaries and need only this one lookup.
            if (!legoMode && mipLevel == 0u
                && cellX + 1 < levelW && cellZ + 1 < levelH) {
                h = max(h, f32(textureLoad(
                    heightTex, vec2<i32>(cellX + 1, cellZ), 0).x)
                    - originY);
                h = max(h, f32(textureLoad(
                    heightTex, vec2<i32>(cellX, cellZ + 1), 0).x)
                    - originY);
                h = max(h, f32(textureLoad(
                    heightTex, vec2<i32>(cellX + 1, cellZ + 1), 0).x)
                    - originY);
            }
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Potential Intersection Check
        // ─────────────────────────────────────────────────────────────────────
        // If ray Y (at either entry or exit) is below the max height, potential hit
        // Lego Mode: Expand height check to include studs
        let hCheck = select(h, h + toHeightmapScale(cellScale * 0.25), legoMode && mipLevel == 0u);

        if (min(yEnter, yExit) <= hCheck) {
            // Side-hit adjustment: if descending ray enters above terrain,
            // compute exact t where ray crosses terrain height

            var hitFound = false;

            if (legoMode) {
                if (mipLevel == 0u) {
                    // Logic for Lego intersection
                    // If we hit the stud or the brick, we count it as a hit.
                    // If we miss both, we do NOT set hitFound, and we do NOT descend (already at 0).
                    // We let the loop continue to next cell.

                    // But wait, the standard logic assumes if we are at mip 0, we hit.
                    // So we must manually check geometry here.

                    var hitLego = false;

                    // 1. Brick Body (y <= h)
                    if (min(yEnter, yExit) <= h) {
                        // Standard hit logic for the blocky part
                        if (slopeY < 0.0 && yEnter > h) {
                            // Hit top of brick
                            t = h / slopeY;
                        }
                        // Else hit side (t is already correct)
                        hitLego = true;
                    }

                    // 2. Stud Intersection
                    let studHeightWorld = cellScale * 0.2;
                    let studRadiusWorld = cellScale * 0.35;
                    // Reconstruct world Y of brick surface
                    let h_map = (h + originY) / 65535.0;
                    let brickY = (h_map * 2.0 - 1.0) * camera.metrics.x;
                    let cellCenterXZ = (vec2<f32>(f32(cellX)+0.5, f32(cellZ)+0.5)) * cellScale - terrainOrigin;
                    let studBasePos = vec3<f32>(cellCenterXZ.x, brickY, cellCenterXZ.y);

                    let tStud = intersectStud(origin, dir, studBasePos, studHeightWorld, studRadiusWorld);

                    if (tStud > 1e-3 && tStud < tNext) {
                        // If we hit stud closer than brick (or if we didn't hit brick)
                        if (!hitLego || tStud < t) {
                            t = tStud;
                            hitLego = true;
                        }
                    }

                    if (hitLego) {
                        hitFound = true;
                    }
                } else {
                    // Force descent if Lego mode enabled (handled by standard descend logic below)
                    // Just ensure we don't set hitFound = true prematurely for LODs
                }
            } else {
                // Coarse maximum-height mips accelerate traversal but never
                // become visible geometry. Refine all the way to a base cell,
                // then solve its bilinear patch exactly.
                if (mipLevel == 0u
                    && cellX + 1 < levelW && cellZ + 1 < levelH) {
                    let refinedHit = intersectBilinearHeightCell(
                        origin, dir, terrainOrigin, cellScale,
                        vec2<i32>(cellX, cellZ), t, tNext,
                        originY, slopeY);
                    if (refinedHit > 0.0) {
                        t = refinedHit;
                        hitFound = true;
                    }
                }
            }
            
            if (hitFound) { break; }
            
            // ─────────────────────────────────────────────────────────────────
            // Descend to Finer Mip Level (Sebbbi's packed approach)
            // ─────────────────────────────────────────────────────────────────
            // Only descend if we are not at mip 0
            if (mipLevel > 0u) {
                mipLevel--;
                tDeltaX *= 0.5;
                tDeltaZ *= 0.5;
                // Refine cell coordinates: shift left doubles both X and Z, add offset
                cellPacked = (cellPacked << 1) + offsetPacked;

                // Adjust cell if t is before the mid-boundary (relative approach)
                if (t < tMaxX - tDeltaX) {
                    tMaxX -= tDeltaX;
                    cellPacked -= stepX;
                }
                if (t < tMaxZ - tDeltaZ) {
                    tMaxZ -= tDeltaZ;
                    cellPacked -= stepZ;
                }
                continue;
            }
            // If mipLevel == 0 and we didn't find a hit (missed lego geometry),
            // we fall through to Next Cell logic.
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Step to Next Cell
        // ─────────────────────────────────────────────────────────────────────
        t = tNext;
        
        // Check if we've exited the terrain bounds
        if (t > range.y) { t = -1.0; break; }
        
        // Advance along the shorter axis (standard 2D DDA)
        if (tMaxX < tMaxZ) {
            tMaxX += tDeltaX;
            cellPacked += stepX;
        } else {
            tMaxZ += tDeltaZ;
            cellPacked += stepZ;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Level-Up Check - Ascend to Coarser Mip (Sebbbi's packed approach)
        // ─────────────────────────────────────────────────────────────────────
        // When ray is far above terrain surface, ascend to coarser mip for faster traversal
        let levelUpHeight = f32(128u << mipLevel);
        if (mipLevel < maxMipLevel && yExit - levelUpHeight > h) {
            mipLevel++;
            // Adjust tMax if we're not at a coarser-level cell boundary
            if ((cellPacked & 1) != offsetX) { tMaxX += tDeltaX; }
            if ((cellPacked & 65536) != offsetZ) { tMaxZ += tDeltaZ; }
            tDeltaX *= 2.0;
            tDeltaZ *= 2.0;
            // Coarsen cell coordinates: mask off low bits then shift right
            cellPacked = i32((u32(cellPacked) & 0xfffefffe) >> 1);
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Shadow Calculation & Output
    // ─────────────────────────────────────────────────────────────────────────
    var shadowFactor = 1.0;
    var waterDepthUnder = 0.0;
    var waterHasTerrainBed = false;
    var shoreInfluence = 0.0;
    var waterShadingWave = vec4<f32>(0.0);
    var material = select(MATERIAL_SKY, MATERIAL_TERRAIN, t > 0.0);

    if (waterHit.distance > 0.0 && waterHit.distance < 50000.0) {
        let tWater = waterHit.distance;
        let surface = waterHit.surface;
        // t == -2.0 is the traversal loop-limit sentinel (hot pink debug).
        // Keep it visible instead of letting a water hit silently replace it.
        if (t > -1.5 && (t <= 0.0 || tWater < t)) {
            let waterPos = origin + dir * tWater;
            let surfaceHeight = waterHeight + surface.height;
            let waterCoord = (waterPos.xz + terrainOrigin) / cellScale;
            let waterCell = vec2<i32>(floor(waterCoord));
            let baseW = i32(camera.terrainSize.x);
            let baseH = i32(camera.terrainSize.y);
            let baseSize = vec2<i32>(baseW, baseH);
            if (waterCell.x >= 1 && waterCell.y >= 1 &&
                waterCell.x < baseW - 1 && waterCell.y < baseH - 1) {
                let terrainHeightRaw = f32(textureLoad(heightTex, waterCell, 0).x);
                let terrainHeightWorld = heightmapToWorldHeight(terrainHeightRaw);
                if (terrainHeightWorld <
                    max(surfaceHeight,
                        waterHeight + SHORE_SURFACE_OVERLAP) -
                        MIN_WATER_DEPTH) {
                    t = tWater;
                    material = MATERIAL_WATER;
                    waterHasTerrainBed = true;
                    waterShadingWave = surface.shadingWave;
                    let realDepth = max(
                        surfaceHeight - terrainHeightWorld, MIN_WATER_DEPTH);
                    shoreInfluence = nearbyShoreInfluence(waterCell, baseSize, surfaceHeight);
                    waterDepthUnder = mix(realDepth, min(realDepth, SHORE_DEPTH), shoreInfluence);
                }
            } else {
                t = tWater;
                material = MATERIAL_WATER;
                waterShadingWave = surface.shadingWave;
                waterDepthUnder = 500.0;
            }
        }
    }

    // Shadows for both terrain and water surfaces so canyon walls cast
    // shadows onto the river just like they do onto the ground.
    if (t > 0.0 &&
        (material == MATERIAL_TERRAIN ||
         (material == MATERIAL_WATER && waterHasTerrainBed))) {
        let hitPos = origin + dir * t;

        if (legoMode) {
            let lightDir = camera.lightDirWS.xyz;
            shadowFactor = sampleLegoShadow(
                hitPos, lightDir, terrainOrigin, cellScale, t);
        } else {
            // Static sun + static terrain: one baked-texture lookup replaces
            // the whole shadow DDA.
            shadowFactor = sampleBakedShadow(hitPos, terrainOrigin, cellScale);
        }
    }

    // Terrain stores its binary shadow directly. Water packs depth in the
    // magnitude and the shadow bit in the sign: +(depth+1) is lit,
    // -(depth+1) is shadowed. This halves output bandwidth and uses the
    // universally supported R32Float storage format.
    var packedShadow = shadowFactor;
    if (material == MATERIAL_WATER) {
        packedShadow = select(-(waterDepthUnder + 1.0), waterDepthUnder + 1.0,
                              shadowFactor > 0.5);
    }
    textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(t, 0.0, 0.0, 0.0));
    textureStore(outShadow, vec2<i32>(gid.xy), vec4<f32>(packedShadow, 0.0, 0.0, 0.0));
    if (material == MATERIAL_WATER) {
        // Lighting consumes the already-evaluated normal and crest. The
        // fourth channel carries the same shoreline influence as before.
        textureStore(outMaterial, vec2<i32>(gid.xy),
                     vec4<f32>(waterShadingWave.y, waterShadingWave.z,
                               waterShadingWave.w, shoreInfluence));
    } else if (material == MATERIAL_TERRAIN) {
        // Preserve the smoothly interpolated heightfield derivative.
        // Reconstructing terrain normals from neighboring screen depths
        // exaggerates one-metre cell boundaries at grazing angles.
        let terrainNormal = terrainSurfaceNormal(
            origin + dir * t, terrainOrigin, cellScale);
        textureStore(outMaterial, vec2<i32>(gid.xy),
                     vec4<f32>(terrainNormal, 1.0));
    }
}
