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
};

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

    // Use Mip Level 2 for faster shadow traversal
    // Requires that heightTex has at least 3 mip levels (0, 1, 2)
    var mipLevel : u32 = 2u;

    var pos = origin + dir * t;
    let cellScaleMip = cellScale * f32(1u << mipLevel);
    let sizeI = vec2<i32>(i32(camera.terrainSize.x) >> mipLevel,
                          i32(camera.terrainSize.y) >> mipLevel);

    var cellX = clamp(i32(floor((pos.x + terrainOrigin.x) / cellScaleMip)), 0, sizeI.x - 1);
    var cellZ = clamp(i32(floor((pos.z + terrainOrigin.y) / cellScaleMip)), 0, sizeI.y - 1);

    let stepX = select(-1, 1, dir.x >= 0.0);
    let stepZ = select(-1, 1, dir.z >= 0.0);
    let offsetX = select(0, 1, dir.x >= 0.0);
    let offsetZ = select(0, 1, dir.z >= 0.0);

    let nextBoundaryX = (f32(cellX + offsetX) * cellScaleMip) - terrainOrigin.x;
    let nextBoundaryZ = (f32(cellZ + offsetZ) * cellScaleMip) - terrainOrigin.y;
    var tMaxX = select(1e30, (nextBoundaryX - pos.x) / dir.x + t, dir.x != 0.0);
    var tMaxZ = select(1e30, (nextBoundaryZ - pos.z) / dir.z + t, dir.z != 0.0);
    var tDeltaX = select(1e30, cellScaleMip / abs(dir.x), dir.x != 0.0);
    var tDeltaZ = select(1e30, cellScaleMip / abs(dir.z), dir.z != 0.0);

    let slopeY = toHeightmapScale(dir.y);
    let originY = toHeightmapCoordinate(origin.y);

    var loopCount = 0u;
    loop {
        loopCount++;
        if (loopCount > 200u) { break; }

        var tNext = min(tMaxX, tMaxZ);
        var yEnter = slopeY * t;
        var yExit = slopeY * tNext;

        var h = -1.0e30;
        let levelW = i32(camera.terrainSize.x) >> mipLevel;
        let levelH = i32(camera.terrainSize.y) >> mipLevel;

        if (cellX >= 0 && cellX < levelW && cellZ >= 0 && cellZ < levelH) {
             h = f32(textureLoad(heightTex, vec2<i32>(cellX, cellZ), i32(mipLevel)).x) - originY;
        }

        if (min(yEnter, yExit) <= h) {
             return 0.0;
        }

        t = tNext;
        if (t > range.y) { break; }

        if (tMaxX < tMaxZ) {
            tMaxX += tDeltaX;
            cellX += stepX;
        } else {
            tMaxZ += tDeltaZ;
            cellZ += stepZ;
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

const TILE_QUADS : u32 = 64u;
const TILE_VERTS : u32 = TILE_QUADS + 1u;

@vertex
fn vs(@builtin(vertex_index) vid : u32, @builtin(instance_index) iid : u32) -> VSOut {
    
    let step = max(u32(camera.metrics.z), 1u);
    let terrainSize = vec2<u32>(u32(camera.terrainSize.x), u32(camera.terrainSize.y));
    
    let quadsX = max(((terrainSize.x - 1u) + step - 1u) / step, 1u);
    let quadsY = max(((terrainSize.y - 1u) + step - 1u) / step, 1u);
    let tilesX = max((quadsX + TILE_QUADS - 1u) / TILE_QUADS, 1u);
    let tilesY = max((quadsY + TILE_QUADS - 1u) / TILE_QUADS, 1u);
    
    let tileID = visibleIndices[iid];

    let tileX = tileID % tilesX;
    let tileY = tileID / tilesX;
    let localX = vid % TILE_VERTS;
    let localY = vid / TILE_VERTS;
    
    let rawCoord = (vec2<u32>(tileX, tileY) * TILE_QUADS + vec2<u32>(localX, localY)) * step;
    let coord = min(rawCoord, terrainSize - vec2<u32>(1u, 1u));
    
    let heightScale = camera.metrics.x;
    let cellScale = camera.metrics.y;
    let height = sampleHeight(vec2<i32>(coord)) * heightScale;
    let origin = 0.5 * (vec2<f32>(camera.terrainSize) - vec2<f32>(1.0, 1.0)) * cellScale;
    let worldPos = vec3<f32>(
        f32(coord.x) * cellScale - origin.x,
        height,
        f32(coord.y) * cellScale - origin.y
    );
    
    let s = i32(step);
    let hL = sampleHeight(vec2<i32>(i32(coord.x) - s, i32(coord.y))) * heightScale;
    let hR = sampleHeight(vec2<i32>(i32(coord.x) + s, i32(coord.y))) * heightScale;
    let hD = sampleHeight(vec2<i32>(i32(coord.x), i32(coord.y) - s)) * heightScale;
    let hU = sampleHeight(vec2<i32>(i32(coord.x), i32(coord.y) + s)) * heightScale;
    let dx = vec3<f32>(2.0 * cellScale * f32(step), hR - hL, 0.0);
    let dz = vec3<f32>(0.0, hU - hD, 2.0 * cellScale * f32(step));
    let normal = normalize(cross(dz, dx));
    
    var out : VSOut;
    out.worldPos = worldPos;
    out.normal = normal;
    out.pos = camera.viewProj * vec4<f32>(worldPos, 1.0);
    out.uv = vec2<f32>(coord) / vec2<f32>(terrainSize);
    
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
    // Ray Blit logic: albedo * (diffuse * shadow * lightmap + ambient)
    let litColor = albedo.rgb * (diffuse * shadow * lightmap + ambient);
    
    // Fog
    let fogDensity = max(camera.metrics.w, 0.0);
    let fogColor = vec3<f32>(0.6, 0.68, 0.76);
    let dist = length(input.worldPos - camera.cameraPos.xyz);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.7);
    let finalColor = mix(litColor, fogColor, fogFactor);
    
    return vec4<f32>(finalColor, 1.0);
}
