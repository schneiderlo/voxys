// ═══════════════════════════════════════════════════════════════════════════════
// terrain.wgsl - Triangle Path Terrain Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Renders heightfield terrain as a triangle mesh using tiled instancing.
// Features:
//   - Vertex shader with tiled instancing (64×64 quad tiles)
//   - Height sampling from R16Uint texture
//   - Normal computation via central differences
//   - Fragment shader with basic directional lighting and Ray-Traced Shadows
//   - Height-based color gradient
//   - Exponential distance fog
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
    invProjParams : vec4<f32>,    // Inverse projection params (.xy used)
    lightDirVS : vec4<f32>,       // View-space light direction (.xyz)
    frustumPlanes : array<vec4<f32>, 6>, // Frustum planes (added to match C++ alignment)
    lightDirWS : vec4<f32>,       // World-space light direction (.xyz)
    waterParams : vec4<f32>,      // (height, enabled, waveStrength, roughness)
    waterColorA : vec4<f32>,      // shallow color rgb, reflection strength
    waterColorB : vec4<f32>,      // deep color rgb, shore fade depth
    waterMotion : vec4<f32>,      // simulation time, reserved...
    lightingColor : vec4<f32>,    // sun colour rgb, intensity
    ambientExposure : vec4<f32>,  // ambient colour rgb, exposure
    fogColor : vec4<f32>,         // atmospheric fog colour
    waterOptics : vec4<f32>,      // IOR, distortion, absorption, scatter
    waterFoam : vec4<f32>,        // size, opacity, coverage, reflection distance
    waterSpectrum : vec4<f32>,    // broad/detail patch lengths
};

// Beer-Lambert extinction per world unit of water (matches ray_blit.wgsl).
const WATER_EXTINCTION : vec3<f32> = vec3<f32>(0.135, 0.052, 0.033);

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
// Note: heightTex must have mipmaps generated for shadow calculation to work correctly.
// The engine ensures this by uploading the heightmap with a full mip chain.
@group(0) @binding(1) var heightTex : texture_2d<u32>;
@group(0) @binding(2) var albedoTex : texture_2d<f32>;
@group(0) @binding(3) var lightmapTex : texture_2d<f32>;
@group(0) @binding(4) var texSampler : sampler;
@group(0) @binding(5) var<storage, read> visibleIndices : array<u32>;

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Sample height from heightmap texture at given coordinates
/// Returns height in range [-1, 1]
fn sampleHeight(coord : vec2<i32>) -> f32 {
    let size = vec2<i32>(i32(camera.terrainSize.x), i32(camera.terrainSize.y));
    let clamped = clamp(coord, vec2<i32>(0, 0), size - vec2<i32>(1, 1));
    let raw = f32(textureLoad(heightTex, clamped, 0).x);
    let normalized = raw / 65535.0;
    return normalized * 2.0 - 1.0;  // Map to [-1, 1]
}

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate Space Conversion (for Shadow Ray)
// ─────────────────────────────────────────────────────────────────────────────

fn toHeightmapCoordinate(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5 + 0.5;
    return normalized * 65535.0;
}

fn toHeightmapScale(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5;
    return normalized * 65535.0;
}

/// Coarsest level that exists and still has at least one texel on each axis.
/// Small and non-square heightmaps may not provide the seven coarse levels
/// used by the normal production terrain.
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

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Ray Traversal
// ─────────────────────────────────────────────────────────────────────────────

fn intersectAabb(origin : vec3<f32>, dir : vec3<f32>,
                 bmin : vec3<f32>, bmax : vec3<f32>) -> vec2<f32> {
    let invDir = 1.0 / (dir + sign(dir) * 1e-20 + vec3<f32>(1e-20));
    let t0 = (bmin - origin) * invDir;
    let t1 = (bmax - origin) * invDir;
    let tMin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    let tMax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return vec2<f32>(tMin, tMax);
}

/// Coarsest mip the shadow march may resolve to, scaled by distance along
/// the ray. Shadow detail matters most near the receiver; far away, a
/// coarse max-height cell is accepted as an occluder. Slightly conservative
/// (a touch more shadow at range) but skips most fine-mip descents.
fn shadowMinMip(t : f32, cellScale : f32) -> u32 {
    let cells = t / cellScale;
    if (cells < 96.0) { return 0u; }
    if (cells < 256.0) { return 1u; }
    if (cells < 640.0) { return 2u; }
    return 3u;
}

/// Ray-Traced Shadow (ported from terrain_raycast.wgsl)
/// Returns 0.0 if shadowed, 1.0 if lit
fn intersectShadow(origin : vec3<f32>, dir : vec3<f32>) -> f32 {
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

    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        return 1.0;
    }

    var t = max(range.x, 0.0) + 1e-4;
    if (t > range.y) { return 1.0; }

    let maxMipLevel = maxTraversalMip();
    var mipLevel : u32 = 0u;
    var pos = origin + dir * t;
    let cellScaleMip = cellScale * f32(1u << mipLevel);
    let sizeI = vec2<i32>(i32(camera.terrainSize.x) >> mipLevel,
                          i32(camera.terrainSize.y) >> mipLevel);

    let cellXInit = clamp(i32(floor((pos.x + terrainOrigin.x) / cellScaleMip)), 0, sizeI.x - 1);
    let cellZInit = clamp(i32(floor((pos.z + terrainOrigin.y) / cellScaleMip)), 0, sizeI.y - 1);
    var cellPacked = (cellZInit << 16) | cellXInit;

    let stepX = select(-1, 1, dir.x >= 0.0);
    let stepZ = i32(select(-1, 1, dir.z >= 0.0)) << 16;
    let offsetX = select(0, 1, dir.x >= 0.0);
    let offsetZ = select(0, 1, dir.z >= 0.0) << 16;
    let offsetPacked = offsetZ | offsetX;

    let nextBoundaryX = (f32(cellXInit + offsetX) * cellScaleMip) - terrainOrigin.x;
    let nextBoundaryZ = (f32(cellZInit + (offsetZ >> 16)) * cellScaleMip) - terrainOrigin.y;
    let ddaEpsilon = 1e-6;
    var tMaxX = select(1e30, (nextBoundaryX - pos.x) / dir.x + t, abs(dir.x) > ddaEpsilon);
    var tMaxZ = select(1e30, (nextBoundaryZ - pos.z) / dir.z + t, abs(dir.z) > ddaEpsilon);
    var tDeltaX = select(1e30, cellScaleMip / abs(dir.x), abs(dir.x) > ddaEpsilon);
    var tDeltaZ = select(1e30, cellScaleMip / abs(dir.z), abs(dir.z) > ddaEpsilon);

    let slopeY = toHeightmapScale(dir.y);
    let originY = toHeightmapCoordinate(origin.y);

    var loopCount = 0u;
    loop {
        loopCount++;
        if (loopCount > 1024u) { break; }

        var tNext = min(tMaxX, tMaxZ);
        var yEnter = slopeY * t;
        var yExit = slopeY * tNext;

        var h = -1.0e30;
        let levelW = i32(camera.terrainSize.x) >> mipLevel;
        let levelH = i32(camera.terrainSize.y) >> mipLevel;
        let cellX = cellPacked & 0xffff;
        let cellZ = cellPacked >> 16;

        if (cellX >= 0 && cellX < levelW && cellZ >= 0 && cellZ < levelH) {
             h = f32(textureLoad(heightTex, vec2<i32>(cellX, cellZ), i32(mipLevel)).x) - originY;
        }

        if (min(yEnter, yExit) <= h) {
            if (mipLevel == 0u) {
                // The interpolated triangle can sit slightly below the source
                // heightfield. Ignore its first cell to avoid self-shadow acne.
                if (loopCount > 1u) { return 0.0; }
            } else {
                // Far from the receiver, accept the coarse cell as a blocker
                // instead of descending all the way to mip 0.
                if (mipLevel <= shadowMinMip(t, cellScale)) { return 0.0; }
                mipLevel--;
                tDeltaX *= 0.5;
                tDeltaZ *= 0.5;
                cellPacked = (cellPacked << 1) + offsetPacked;

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

        t = tNext;
        if (t > range.y) { break; }

        if (tMaxX < tMaxZ) {
            tMaxX += tDeltaX;
            cellPacked += stepX;
        } else {
            tMaxZ += tDeltaZ;
            cellPacked += stepZ;
        }

        // Max-height mips skip empty space, but every possible blocker descends
        // back to mip zero before being accepted, preserving sharp shadows.
        if (mipLevel < maxMipLevel) {
            let levelUpHeight = f32(128u << mipLevel);
            if (yExit - levelUpHeight > h) {
                mipLevel++;
                if ((cellPacked & 1) != offsetX) { tMaxX += tDeltaX; }
                if ((cellPacked & 65536) != offsetZ) { tMaxZ += tDeltaZ; }
                tDeltaX *= 2.0;
                tDeltaZ *= 2.0;
                cellPacked = i32((u32(cellPacked) & 0xfffefffeu) >> 1u);
            }
        }
    }
    return 1.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader
// ─────────────────────────────────────────────────────────────────────────────

struct VSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) worldPos : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) uv : vec2<f32>,
};

const TILE_QUADS : u32 = 32u;
const TILE_VERTS : u32 = TILE_QUADS + 1u;
const BASE_VERTEX_COUNT : u32 = TILE_VERTS * TILE_VERTS;

@vertex
fn vs(@builtin(vertex_index) vid : u32, @builtin(instance_index) iid : u32) -> VSOut {
    let terrainSize = vec2<u32>(u32(camera.terrainSize.x), u32(camera.terrainSize.y));
    let packedTile = visibleIndices[iid];
    let tileX = packedTile & 0x3ffu;
    let tileY = (packedTile >> 10u) & 0x3ffu;
    let lod = (packedTile >> 20u) & 0x3u;
    let transitionEdgeMask = (packedTile >> 22u) & 0xfu;
    let step = max(u32(camera.metrics.z), 1u) << lod;

    var localX = 0u;
    var localY = 0u;
    var skirtEdge = 4u;
    if (vid < BASE_VERTEX_COUNT) {
        localX = vid % TILE_VERTS;
        localY = vid / TILE_VERTS;
    } else {
        let skirtVertex = vid - BASE_VERTEX_COUNT;
        skirtEdge = skirtVertex / TILE_VERTS;
        let edgeOffset = skirtVertex % TILE_VERTS;
        switch skirtEdge {
            case 0u: { localX = edgeOffset; localY = 0u; }
            case 1u: { localX = TILE_QUADS; localY = edgeOffset; }
            case 2u: { localX = TILE_QUADS - edgeOffset; localY = TILE_QUADS; }
            default: { localX = 0u; localY = TILE_QUADS - edgeOffset; }
        }
    }

    let rawCoord = (vec2<u32>(tileX, tileY) * TILE_QUADS + vec2<u32>(localX, localY)) * step;
    let coord = min(rawCoord, terrainSize - vec2<u32>(1u, 1u));

    let heightScale = camera.metrics.x;
    let cellScale = camera.metrics.y;
    var height = sampleHeight(vec2<i32>(coord)) * heightScale;

    let s = i32(step);
    let hL = sampleHeight(vec2<i32>(i32(coord.x) - s, i32(coord.y))) * heightScale;
    let hR = sampleHeight(vec2<i32>(i32(coord.x) + s, i32(coord.y))) * heightScale;
    let hD = sampleHeight(vec2<i32>(i32(coord.x), i32(coord.y) - s)) * heightScale;
    let hU = sampleHeight(vec2<i32>(i32(coord.x), i32(coord.y) + s)) * heightScale;
    let dx = vec3<f32>(2.0 * cellScale * f32(step), hR - hL, 0.0);
    let dz = vec3<f32>(0.0, hU - hD, 2.0 * cellScale * f32(step));
    var normal = normalize(cross(dz, dx));

    if (skirtEdge < 4u) {
        let edgeEnabled = (transitionEdgeMask & (1u << skirtEdge)) != 0u;
        height -= select(0.0, max(cellScale * f32(step) * 2.0, 2.0), edgeEnabled);
        switch skirtEdge {
            case 0u: { normal = vec3<f32>(0.0, 0.0, -1.0); }
            case 1u: { normal = vec3<f32>(1.0, 0.0, 0.0); }
            case 2u: { normal = vec3<f32>(0.0, 0.0, 1.0); }
            default: { normal = vec3<f32>(-1.0, 0.0, 0.0); }
        }
    }

    let origin = 0.5 * (vec2<f32>(camera.terrainSize) - vec2<f32>(1.0, 1.0)) * cellScale;
    let worldPos = vec3<f32>(
        f32(coord.x) * cellScale - origin.x,
        height,
        f32(coord.y) * cellScale - origin.y
    );

    var out : VSOut;
    out.worldPos = worldPos;
    out.normal = normal;
    out.pos = camera.viewProj * vec4<f32>(worldPos, 1.0);
    let textureSpan = max(vec2<f32>(terrainSize) - vec2<f32>(1.0), vec2<f32>(1.0));
    out.uv = vec2<f32>(coord) / textureSpan;
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fragment Shader
// ─────────────────────────────────────────────────────────────────────────────

@fragment
fn fs(input : VSOut) -> @location(0) vec4<f32> {
    // Light direction from uniform (was hardcoded)
    let lightDir = normalize(camera.lightDirWS.xyz);
    
    // Ambient intensity
    let ambient = max(camera.lightDirVS.w, 0.05);

    // Calculate shadow (Ray-Traced)
    // Bias: offset origin towards light to avoid self-intersection
    let shadowBias = 1.5;
    let shadowOrigin = input.worldPos + lightDir * shadowBias;
    let shadow = intersectShadow(shadowOrigin, lightDir);

    // Diffuse lighting
    let diffuse = max(dot(input.normal, lightDir), 0.0);

    // Sample textures
    let albedo = textureSample(albedoTex, texSampler, input.uv);
    let lightmap = textureSample(lightmapTex, texSampler, input.uv).r;
    
    // Combine lighting
    // Shadow affects diffuse term. Lightmap (baked sky visibility) modulates diffuse + shadow.
    let sunRadiance = camera.lightingColor.rgb * camera.lightingColor.w;
    let ambientMaximum = max(max(camera.ambientExposure.r,
                                 camera.ambientExposure.g),
                             max(camera.ambientExposure.b, 0.001));
    let ambientTint = camera.ambientExposure.rgb / ambientMaximum;
    var litColor = albedo.rgb *
        (diffuse * shadow * lightmap * sunRadiance + ambient * ambientTint);

    // Fallback water: the triangle path has no water surface geometry, so tint
    // submerged terrain by the column of water above it (Beer-Lambert). This
    // keeps water bodies visible when A/B comparing against the raycast path.
    let waterEnabled = camera.waterParams.y > 0.5;
    let waterHeight = camera.waterParams.x;
    if (waterEnabled && input.worldPos.y < waterHeight) {
        let depthUnder = waterHeight - input.worldPos.y;
        let transmit = exp(-WATER_EXTINCTION * depthUnder);
        // waterColorB.a is the depth over which shallow fades to deep colour.
        let depth01 = clamp(depthUnder / max(camera.waterColorB.a, 0.001), 0.0, 1.0);
        let tint = mix(camera.waterColorA.rgb, camera.waterColorB.rgb, depth01);
        litColor = litColor * transmit + tint * (1.0 - transmit);
    }

    // Fog
    let fogDensity = max(camera.metrics.w, 0.0);
    let dist = length(input.worldPos - camera.cameraPos.xyz);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.7);
    let finalColor = mix(litColor, camera.fogColor.rgb, fogFactor) *
                     camera.ambientExposure.w;
    
    return vec4<f32>(finalColor, 1.0);
}
