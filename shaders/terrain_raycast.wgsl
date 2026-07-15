// ═══════════════════════════════════════════════════════════════════════════════
// terrain_raycast.wgsl - Compute Ray-Caster Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Performs hierarchical DDA ray-casting against the heightfield mip pyramid,
// outputting linear ray distance per pixel.
// Features:
//   - Ray generation from pixel coordinates using inverse view-projection
//   - AABB intersection for early ray clipping
//   - Hierarchical DDA traversal with mip level transitions
//   - Distance-based LOD termination
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
};

// ─────────────────────────────────────────────────────────────────────────────
// Bindings
// ─────────────────────────────────────────────────────────────────────────────

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var heightTex : texture_2d<u32>;
@group(0) @binding(2) var outDepth : texture_storage_2d<r32float, write>;
@group(0) @binding(3) var outShadow : texture_storage_2d<r32float, write>;
// Water-only auxiliary output: material id plus shoreline influence.
@group(0) @binding(4) var outMaterial : texture_storage_2d<r32float, write>;
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
const MIN_WATER_DEPTH : f32 = 0.5;
const SHORE_DEPTH : f32 = 7.5;
const WATER_SURFACE_AMPLITUDE : f32 = 1.0;
const WATER_TAU : f32 = 6.283185307179586;
const WATER_GRAVITY : f32 = 9.81;
const WATER_INCOMING_DIRECTION : vec2<f32> = vec2<f32>(0.9100, 0.4146);

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

fn lodDistanceForMip(level : u32) -> f32 {
    switch level {
        case 0u: { return 0.0; }
        case 1u: { return 1096.6332; }
        case 2u: { return 2980.9580; }
        case 3u: { return 8103.0839; }
        case 4u: { return 22026.4658; }
        case 5u: { return 59874.1417; }
        case 6u: { return 162754.7914; }
        default: { return 442413.3920; }
    }
}

struct CoastalWave {
    height : f32,
    blend : f32,
    exposure : f32,
};

fn coastFieldUv(worldXZ : vec2<f32>) -> vec2<f32> {
    let cells = max(camera.terrainSize - vec2<f32>(1.0), vec2<f32>(1.0));
    let extent = cells * camera.metrics.y;
    let rawUv = (worldXZ + extent * 0.5) / extent;
    // The FFT sampler repeats. Keep coast samples inside the edge texels so
    // the far side of the terrain cannot wrap into this shoreline.
    let dims = vec2<f32>(textureDimensions(waterCoastFieldTex));
    let halfTexel = 0.5 / dims;
    return clamp(rawUv, halfTexel, vec2<f32>(1.0) - halfTexel);
}

fn coastalWaveField(worldXZ : vec2<f32>) -> CoastalWave {
    let coast = textureSampleLevel(waterCoastFieldTex,
        waterDisplacementSampler, coastFieldUv(worldXZ), 0.0);
    let coastDistance = max(coast.z, 0.0);
    let waterDepth = max(coast.w, 0.0);
    let rawOnshore = coast.xy;
    let directionalExposure = clamp(length(rawOnshore), 0.0, 1.0);
    var onshore = WATER_INCOMING_DIRECTION;
    if (dot(rawOnshore, rawOnshore) > 0.01) {
        onshore = normalize(rawOnshore);
    }

    // Offshore phase follows the incoming swell. Across a broad coastal band,
    // its phase coordinate becomes distance-to-shore, whose contours are
    // naturally parallel to beaches, coves, and islands.
    let turn = 1.0 - smoothstep(55.0, 420.0, coastDistance);
    let wet = smoothstep(0.55, 2.8, waterDepth);
    let facing = smoothstep(-0.20, 0.55,
                            dot(WATER_INCOMING_DIRECTION, onshore));
    let coastResponse = directionalExposure * mix(0.12, 1.0, facing);
    let blend = turn * wet * coastResponse;
    let shelterInfluence = 1.0 - smoothstep(220.0, 850.0, coastDistance);
    let waveExposure = mix(1.0, max(0.16, directionalExposure), shelterInfluence);
    if (blend == 0.0) {
        return CoastalWave(0.0, 0.0, waveExposure);
    }
    let shallow = 1.0 - smoothstep(4.0, 28.0, waterDepth);
    let wavelengthCompression = mix(1.0, 1.58, shallow);
    let offshoreCoordinate = -dot(worldXZ, WATER_INCOMING_DIRECTION);
    let phaseCoordinate = mix(offshoreCoordinate, coastDistance, turn) +
                          coastDistance * (wavelengthCompression - 1.0) * turn;
    let tangent = vec2<f32>(-onshore.y, onshore.x);
    let alongshore = dot(worldXZ, tangent);

    let k0 = WATER_TAU / 27.0;
    let k1 = WATER_TAU / 12.5;
    let phase0 = k0 * phaseCoordinate + sqrt(WATER_GRAVITY * k0) *
                 camera.waterMotion.x + 0.20 * sin(alongshore * 0.031);
    let phase1 = k1 * phaseCoordinate + sqrt(WATER_GRAVITY * k1) *
                 camera.waterMotion.x + 1.7 + 0.12 * sin(alongshore * 0.067);
    let shoaling = mix(1.0, 1.42, shallow);
    let amplitude0 = 0.62 * shoaling * wet;
    let amplitude1 = 0.19 * mix(1.0, 1.20, shallow) * wet;
    let height = sin(phase0) * amplitude0 + sin(phase1) * amplitude1;
    return CoastalWave(height, blend, waveExposure);
}

fn waterSurfaceOffset(worldXZ : vec2<f32>) -> f32 {
    let strength = clamp(camera.waterParams.z, 0.0, 1.0);
    let shortWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, worldXZ / 96.0, 0, 0.0).x;
    let mediumWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, worldXZ / 384.0, 1, 0.0).x;
    let longWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, worldXZ / 1536.0, 2, 0.0).x;
    let fftHeight = shortWaves + mediumWaves + longWaves;
    let coast = coastalWaveField(worldXZ);
    return mix(fftHeight * coast.exposure, coast.height, coast.blend) *
           WATER_SURFACE_AMPLITUDE * strength;
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
//      a. If at LOD-appropriate level for distance, accept hit
//      b. Otherwise descend to finer mip level
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
    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        // The blit classifies sky from negative depth and never observes the
        // other outputs for these pixels.
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
                if (slopeY < 0.0 && yEnter > h) {
                    t = h / slopeY;
                }

                // Constant distance thresholds avoid a transcendental operation
                // inside the divergent traversal loop.
                if (t >= lodDistanceForMip(mipLevel)) {
                    hitFound = true;
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
    var shoreInfluence = 0.0;
    var material = select(MATERIAL_SKY, MATERIAL_TERRAIN, t > 0.0);

    let waterEnabled = camera.waterParams.y > 0.5;
    let waterHeight = camera.waterParams.x;
    if (waterEnabled && abs(dir.y) > 1e-5) {
        // Flat-plane intersection first, then iterate the wave-surface
        // intersection. The wave height depends on the hit position, so a
        // single flat-plane sample warps at steep viewing angles.
        var tWater = (waterHeight - origin.y) / dir.y;
        // The fixed-point refinement only converges when the ray is steep
        // relative to the wave slope: each iteration moves t by up to
        // amplitude / |dir.y|, which explodes near the horizon and makes
        // neighboring pixels land on different "surfaces" (shimmer, holes).
        // At grazing angles the flat plane is the stable answer and the wave
        // offset is sub-pixel anyway.
        if (abs(dir.y) > 0.02) {
            for (var iter = 0u; iter < 2u; iter++) {
                let p = origin + dir * tWater;
                let h = waterHeight + waterSurfaceOffset(p.xz);
                tWater = (h - origin.y) / dir.y;
            }
        }
        // t == -2.0 is the traversal loop-limit sentinel (hot pink debug).
        // Keep it visible instead of letting a water hit silently replace it.
        if (t > -1.5 && tWater > max(range.x, 0.0) && tWater < range.y &&
            (t <= 0.0 || tWater < t)) {
            let surfaceHeight = waterHeight +
                waterSurfaceOffset((origin + dir * tWater).xz);
            let waterPos = origin + dir * tWater;
            let waterCoord = (waterPos.xz + terrainOrigin) / cellScale;
            let waterCell = vec2<i32>(floor(waterCoord));
            let baseW = i32(camera.terrainSize.x);
            let baseH = i32(camera.terrainSize.y);
            let baseSize = vec2<i32>(baseW, baseH);
            if (waterCell.x >= 0 && waterCell.y >= 0 &&
                waterCell.x < baseW && waterCell.y < baseH) {
                let terrainHeightRaw = f32(textureLoad(heightTex, waterCell, 0).x);
                let terrainHeightWorld = heightmapToWorldHeight(terrainHeightRaw);
                if (terrainHeightWorld < surfaceHeight - MIN_WATER_DEPTH) {
                    t = tWater;
                    material = MATERIAL_WATER;
                    let realDepth = surfaceHeight - terrainHeightWorld;
                    shoreInfluence = nearbyShoreInfluence(waterCell, baseSize, surfaceHeight);
                    waterDepthUnder = mix(realDepth, min(realDepth, SHORE_DEPTH), shoreInfluence);
                }
            }
        }
    }

    // Shadows for both terrain and water surfaces so canyon walls cast
    // shadows onto the river just like they do onto the ground.
    if (t > 0.0 && (material == MATERIAL_TERRAIN || material == MATERIAL_WATER)) {
        let hitPos = origin + dir * t;

        if (legoMode) {
            // Studs are not in the baked field: keep the ray-marched shadow.
            let lightDir = camera.lightDirWS.xyz;
            let shadowOrigin = hitPos + lightDir * (camera.metrics.y * 0.2);
            shadowFactor = intersectShadow(shadowOrigin, lightDir);
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
        // Only water consumes this output. Shore influence rides in the
        // fraction so the blit need not re-derive it with extra texture taps.
        let packedMaterial = f32(MATERIAL_WATER) + shoreInfluence * 0.49;
        textureStore(outMaterial, vec2<i32>(gid.xy),
                     vec4<f32>(packedMaterial, 0.0, 0.0, 0.0));
    }
}
