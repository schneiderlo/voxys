# Shader Specifications

> **Project:** voxy  
> **Version:** 1.0  
> **Status:** Draft

---

## Overview

All shaders in voxy are written in WGSL (WebGPU Shading Language). This document provides complete specifications for each shader file.

---

## 1. Shader Files

| File | Type | Purpose |
|------|------|---------|
| `terrain.wgsl` | Vertex + Fragment | Triangle mesh terrain path |
| `terrain_raycast.wgsl` | Compute | Heightfield ray-caster |
| `ray_blit.wgsl` | Vertex + Fragment | Fullscreen lighting pass |
| `mip_generate.wgsl` | Compute | Max-height mip generation |

---

## 2. Unified Camera Uniforms

All shaders share a single unified `CameraUniforms` structure that packs camera, terrain, and lighting parameters:

```wgsl
struct CameraUniforms {
    viewProj : mat4x4<f32>,       // View-projection matrix
    invViewProj : mat4x4<f32>,    // Inverse view-projection
    invView : mat4x4<f32>,        // Inverse view matrix
    terrainSize : vec2<f32>,      // Heightmap dimensions (e.g., 8192, 8192)
    invTerrainSize : vec2<f32>,   // 1.0 / terrainSize
    metrics : vec4<f32>,          // Packed parameters (see below)
    cameraPos : vec4<f32>,        // World-space camera position (.xyz)
    invProjParams : vec4<f32>,    // Inverse projection params (.xy used)
    lightDirVS : vec4<f32>,       // View-space light direction (.xyz)
};
```

### 2.1 Metrics Vector Layout

The `metrics` field packs four scalar parameters:

| Component | Name | Description |
|-----------|------|-------------|
| `metrics.x` | `heightScale` | World-space height range (e.g., 500.0) |
| `metrics.y` | `cellScale` | World-space size per heightmap cell (e.g., 1.0) |
| `metrics.z` | `step` | LOD step for triangle path (1, 2, 4, etc.) |
| `metrics.w` | `fogDensity` | Exponential fog density |

### 2.2 Uniform Buffer Layout (C++ Side)

```cpp
struct CameraUniforms {
    glm::mat4 viewProj;           // offset: 0,   size: 64
    glm::mat4 invViewProj;        // offset: 64,  size: 64
    glm::mat4 invView;            // offset: 128, size: 64
    glm::vec2 terrainSize;        // offset: 192, size: 8
    glm::vec2 invTerrainSize;     // offset: 200, size: 8
    glm::vec4 metrics;            // offset: 208, size: 16
    glm::vec4 cameraPos;          // offset: 224, size: 16
    glm::vec4 invProjParams;      // offset: 240, size: 16
    glm::vec4 lightDirVS;         // offset: 256, size: 16
    // Total: 272 bytes (aligned to 16)
};
```

---

## 3. terrain.wgsl (Triangle Path)

### 3.1 Purpose

Renders the heightfield as a traditional triangle mesh with LOD support via configurable step size. Each tile is a 64×64 grid of quads rendered via instancing.

### 3.2 Bindings

```wgsl
@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var heightTex : texture_2d<u32>;
```

### 3.3 Helper Functions

```wgsl
fn sampleHeight(coord : vec2<i32>) -> f32 {
    let size = vec2<i32>(i32(camera.terrainSize.x), i32(camera.terrainSize.y));
    let clamped = clamp(coord, vec2<i32>(0, 0), size - vec2<i32>(1, 1));
    let raw = f32(textureLoad(heightTex, clamped, 0).x);
    let normalized = raw / 65535.0;
    return normalized * 2.0 - 1.0;  // Map to [-1, 1]
}
```

### 3.4 Vertex Shader

Key features:
- Dynamic tile count based on LOD `step` parameter
- Terrain centered using `origin = 0.5 * (terrainSize - 1) * cellScale`
- Normals computed via central differences with step-aware sampling

```wgsl
struct VSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) worldPos : vec3<f32>,
    @location(1) normal : vec3<f32>,
};

@vertex
fn vs(@builtin(vertex_index) vid : u32, @builtin(instance_index) iid : u32) -> VSOut {
    const tileQuads : u32 = 64u;
    const tileVerts : u32 = tileQuads + 1u;
    
    let step = max(u32(camera.metrics.z), 1u);
    let terrainSize = vec2<u32>(u32(camera.terrainSize.x), u32(camera.terrainSize.y));
    
    // Compute tile grid dimensions based on step
    let quadsX = max(((terrainSize.x - 1u) + step - 1u) / step, 1u);
    let quadsY = max(((terrainSize.y - 1u) + step - 1u) / step, 1u);
    let tilesX = max((quadsX + tileQuads - 1u) / tileQuads, 1u);
    let tilesY = max((quadsY + tileQuads - 1u) / tileQuads, 1u);
    
    // Decode tile and local position
    let tileX = iid % tilesX;
    let tileY = iid / tilesX;
    let localX = vid % tileVerts;
    let localY = vid / tileVerts;
    
    // Global heightmap coordinate with step
    let rawCoord = (vec2<u32>(tileX, tileY) * tileQuads + vec2<u32>(localX, localY)) * step;
    let coord = min(rawCoord, terrainSize - vec2<u32>(1u, 1u));
    
    // Sample height and compute world position
    let heightScale = camera.metrics.x;
    let cellScale = camera.metrics.y;
    let height = sampleHeight(vec2<i32>(coord)) * heightScale;
    let origin = 0.5 * (vec2<f32>(camera.terrainSize) - vec2<f32>(1.0, 1.0)) * cellScale;
    let worldPos = vec3<f32>(
        f32(coord.x) * cellScale - origin.x,
        height,
        f32(coord.y) * cellScale - origin.y
    );
    
    // Compute normal via central differences
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
    return out;
}
```

### 3.5 Fragment Shader

Features:
- Hardcoded light direction `(0.3, 0.8, 0.4)`
- Height-based color gradient (base green to highlight)
- Fog capped at 0.7 factor

```wgsl
@fragment
fn fs(input : VSOut) -> @location(0) vec4<f32> {
    let lightDir = normalize(vec3<f32>(0.3, 0.8, 0.4));
    let diff = max(dot(input.normal, lightDir), 0.15);
    
    // Height-based color
    let baseColor = vec3<f32>(0.1, 0.35, 0.15);
    let highlight = vec3<f32>(0.45, 0.6, 0.35);
    let heightFactor = clamp(input.worldPos.y / max(camera.metrics.x, 0.0001), 0.0, 1.0);
    let litColor = mix(baseColor, highlight, heightFactor) * diff;
    
    // Fog
    let fogDensity = max(camera.metrics.w, 0.0);
    let fogColor = vec3<f32>(0.6, 0.68, 0.76);
    let dist = length(input.worldPos - camera.cameraPos.xyz);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.7);
    let finalColor = mix(litColor, fogColor, fogFactor);
    
    return vec4<f32>(finalColor, 1.0);
}
```

---

## 4. terrain_raycast.wgsl (Compute Ray-Caster)

### 4.1 Purpose

Performs hierarchical DDA ray-casting against the heightfield mip pyramid, outputting linear ray distance per pixel.

### 4.2 Bindings

```wgsl
@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var heightTex : texture_2d<u32>;
@group(0) @binding(2) var outDepth : texture_storage_2d<r32float, write>;
```

### 4.3 Coordinate Space Conversion

The ray-caster operates in **heightmap coordinate space** for Y comparisons (0-65535 range):

```wgsl
fn toHeightmapCoordinate(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5 + 0.5;
    return normalized * 65535.0;
}

fn toHeightmapScale(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5;
    return normalized * 65535.0;
}
```

### 4.4 AABB Intersection

```wgsl
fn intersectAabb(origin : vec3<f32>, dir : vec3<f32>, 
                 bmin : vec3<f32>, bmax : vec3<f32>) -> vec2<f32> {
    let invDir = 1.0 / dir;
    let t0 = (bmin - origin) * invDir;
    let t1 = (bmax - origin) * invDir;
    let tMin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    let tMax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return vec2<f32>(tMin, tMax);
}
```

### 4.5 Ray Generation

```wgsl
fn rayDirFromPixel(invVP : mat4x4<f32>, pixel : vec2<u32>, 
                   dims : vec2<u32>, origin : vec3<f32>) -> vec3<f32> {
    let dimf = vec2<f32>(f32(dims.x), f32(dims.y));
    let pixelF = vec2<f32>(f32(pixel.x), f32(pixel.y));
    let uv = (pixelF + vec2<f32>(0.5, 0.5)) / dimf;
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let near4 = invVP * vec4<f32>(ndc, 0.0, 1.0);
    let far4 = invVP * vec4<f32>(ndc, 1.0, 1.0);
    let nearPos = near4.xyz / near4.w;
    let farPos = far4.xyz / far4.w;
    return normalize(farPos - origin);
}
```

### 4.6 Main Compute Shader

Key algorithm features:
- **Starting mip level**: 7 (64×64 cells for 8192×8192 terrain)
- **LOD termination**: `f32(mipLevel) <= max(log(t) - 6.0, 0.0)`
- **Level-up threshold**: `128 << mipLevel` heightmap units
- **Heightmap coordinate space**: Y comparisons done in 0-65535 range

```wgsl
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(outDepth);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }
    
    let origin = camera.cameraPos.xyz;
    let dir = rayDirFromPixel(camera.invViewProj, gid.xy, dims, origin);
    
    // Terrain bounds with margin
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
        textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(-1.0, 0.0, 0.0, 0.0));
        return;
    }
    
    // Initialize DDA at mip level 7
    var t = max(range.x, 0.0) + 1e-4;
    var mipLevel : u32 = 7;
    var pos = origin + dir * t;
    
    let cellScaleMip = cellScale * f32(1 << mipLevel);
    let sizeI = vec2<i32>(i32(camera.terrainSize.x) >> mipLevel, 
                          i32(camera.terrainSize.y) >> mipLevel);
    
    // Initialize cell position
    var cellX = clamp(i32(floor((pos.x + terrainOrigin.x) / cellScaleMip)), 0, sizeI.x - 1);
    var cellZ = clamp(i32(floor((pos.z + terrainOrigin.y) / cellScaleMip)), 0, sizeI.y - 1);
    
    // DDA setup
    let stepX = select(-1, 1, dir.x >= 0.0);
    let stepZ = select(-1, 1, dir.z >= 0.0);
    let offsetX = select(0, 1, dir.x >= 0.0);
    let offsetZ = select(0, 1, dir.z >= 0.0);
    
    // Initialize tMax and tDelta
    let nextBoundaryX = (f32(cellX + offsetX) * cellScaleMip) - terrainOrigin.x;
    let nextBoundaryZ = (f32(cellZ + offsetZ) * cellScaleMip) - terrainOrigin.y;
    var tMaxX = select(1e30, (nextBoundaryX - pos.x) / dir.x, dir.x != 0.0);
    var tMaxZ = select(1e30, (nextBoundaryZ - pos.z) / dir.z, dir.z != 0.0);
    var tDeltaX = select(1e30, cellScaleMip / abs(dir.x), dir.x != 0.0);
    var tDeltaZ = select(1e30, cellScaleMip / abs(dir.z), dir.z != 0.0);
    
    // Convert ray Y to heightmap space
    let slopeY = toHeightmapScale(dir.y);
    let originY = toHeightmapCoordinate(origin.y);
    
    // DDA loop
    loop {
        var tNext = min(tMaxX, tMaxZ);
        
        // Sample height and compute ray Y in heightmap space
        var yEnter = slopeY * t;
        var yExit = slopeY * tNext;
        let h = f32(textureLoad(heightTex, vec2<i32>(cellX, cellZ), i32(mipLevel)).x) - originY;
        
        // Potential intersection?
        if (min(yEnter, yExit) <= h) {
            // Side-hit adjustment
            if (slopeY < 0.0 && yEnter > h) {
                t = h / slopeY;
            }
            
            // LOD check: stop if mip level appropriate for distance
            if (f32(mipLevel) <= max(log(t) - 6.0, 0.0)) { break; }
            
            // Descend to finer mip
            mipLevel--;
            tDeltaX *= 0.5;
            tDeltaZ *= 0.5;
            cellX = (cellX << 1) + offsetX;
            cellZ = (cellZ << 1) + offsetZ;
            if (t < tMaxX - tDeltaX) { tMaxX -= tDeltaX; cellX -= stepX; }
            if (t < tMaxZ - tDeltaZ) { tMaxZ -= tDeltaZ; cellZ -= stepZ; }
            continue;
        }
        
        // Step to next cell
        t = tNext;
        if (t > range.y) { t = -1.0; break; }
        
        if (tMaxX < tMaxZ) {
            tMaxX += tDeltaX;
            cellX += stepX;
        } else {
            tMaxZ += tDeltaZ;
            cellZ += stepZ;
        }
        
        // Level-up check: ascend if far from terrain
        let levelUpHeight = select(65535.0, f32(128 << mipLevel), mipLevel < 7);
        if (yExit - levelUpHeight > h) {
            mipLevel++;
            if ((cellX & 1) != offsetX) { tMaxX += tDeltaX; }
            if ((cellZ & 1) != offsetZ) { tMaxZ += tDeltaZ; }
            tDeltaX *= 2.0;
            tDeltaZ *= 2.0;
            cellX = cellX >> 1;
            cellZ = cellZ >> 1;
        }
    }
    
    textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(t, 0.0, 0.0, 0.0));
}
```

### 4.7 Output Encoding

| Value | Meaning |
|-------|---------|
| `t > 0` | Linear ray distance to terrain intersection |
| `t < 0` | Sky (no intersection, specifically `-1.0`) |

---

## 5. ray_blit.wgsl (Fullscreen Lighting)

### 5.1 Purpose

Composites the ray-caster output into a final shaded image with lighting, fog, and sky.

### 5.2 Bindings

```wgsl
@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var depthTex : texture_2d<f32>;
@group(0) @binding(2) var terrainTex : texture_2d<f32>;
@group(0) @binding(3) var lightmapTex : texture_2d<f32>;
@group(0) @binding(4) var terrainSampler : sampler;
```

### 5.3 Vertex Shader (Fullscreen Triangle)

Uses a single oversized triangle to cover the screen:

```wgsl
struct VSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) uv : vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) i : u32) -> VSOut {
    var pos = array<vec2<f32>, 3>(
        vec2(-1.0, -3.0),
        vec2(3.0, 1.0),
        vec2(-1.0, 1.0)
    );
    var uv = array<vec2<f32>, 3>(
        vec2(0.0, 2.0),
        vec2(2.0, 0.0),
        vec2(0.0, 0.0)
    );
    var o : VSOut;
    o.pos = vec4<f32>(pos[i], 0.0, 1.0);
    o.uv = uv[i];
    return o;
}
```

### 5.4 Helper Functions

```wgsl
fn sampleDepth(coords : vec2<i32>) -> f32 {
    let dims = textureDimensions(depthTex, 0);
    let clamped = clamp(coords, vec2<i32>(0, 0), vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1));
    return textureLoad(depthTex, clamped, 0).x;
}

fn ndcFromPixel(coords : vec2<i32>, dims : vec2<f32>) -> vec2<f32> {
    let uv = (vec2<f32>(f32(coords.x), f32(coords.y)) + vec2<f32>(0.5, 0.5)) / dims;
    return vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

fn rayDirFromPixel(invProjParams : vec2<f32>, ndc : vec2<f32>) -> vec3<f32> {
    let dir = vec3<f32>(ndc * invProjParams, -1.0);
    return normalize(dir);
}

fn viewPosFromDepth(invProjParams : vec2<f32>, ndc : vec2<f32>, depth : f32) -> vec3<f32> {
    let dir = rayDirFromPixel(invProjParams, ndc);
    return dir * depth;
}

fn viewToWorld(invView : mat4x4<f32>, viewPos : vec3<f32>) -> vec3<f32> {
    return (invView * vec4<f32>(viewPos, 1.0)).xyz;
}

fn terrainUV(worldPos : vec3<f32>) -> vec2<f32> {
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellCounts = max(terrainSize - vec2<f32>(1.0, 1.0), vec2<f32>(1.0, 1.0));
    let cellScale = max(camera.metrics.y, 0.0001);
    let origin = 0.5 * cellCounts * cellScale;
    let coord = (worldPos.xz + origin) / cellScale;
    return clamp(coord / cellCounts, vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0));
}
```

### 5.5 Fragment Shader

Features:
- Hardcoded sky gradient colors
- Screen-space normal reconstruction (picks closer neighbor)
- Normal flip rule: `if (useNegX == useNegY) { normal = -normal; }`
- Roughness-based specular (0.6 roughness → ~64 power)
- Ambient fixed at 0.30
- Fog capped at 0.7

```wgsl
@fragment
fn fs(i : VSOut) -> @location(0) vec4<f32> {
    let dims = textureDimensions(depthTex, 0);
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let pixF = i.uv * dimsF;
    let pixelI = clamp(vec2<i32>(floor(pixF)), vec2<i32>(0, 0), 
                       vec2<i32>(dims) - vec2<i32>(1, 1));
    let ndcCenter = ndcFromPixel(pixelI, dimsF);
    let depthCenter = sampleDepth(pixelI);
    let invProjParams = camera.invProjParams.xy;
    
    // Sky rendering
    if (depthCenter < 0.0) {
        let viewDir = rayDirFromPixel(invProjParams, ndcCenter);
        let worldDir = (camera.invView * vec4<f32>(viewDir, 0.0)).xyz;
        let skyBottom = vec3<f32>(1.0, 1.0, 0.2);
        let skyTop = vec3<f32>(0.1, 0.1, 1.0);
        let skyFactor = clamp(worldDir.y * 0.5 + 0.5, 0.0, 1.0);
        let sCurve = skyFactor * skyFactor * (3.0 - 2.0 * skyFactor);
        let skyColor = mix(skyBottom, skyTop, sCurve);
        return vec4<f32>(skyColor, 1.0);
    }
    
    // Reconstruct positions
    let posCView = viewPosFromDepth(invProjParams, ndcCenter, depthCenter);
    let posCWorld = viewToWorld(camera.invView, posCView);
    
    // Sample neighbor depths for normal reconstruction
    let offsetX = vec2<i32>(1, 0);
    let offsetY = vec2<i32>(0, 1);
    let depthNegX = sampleDepth(pixelI - offsetX);
    let depthPosX = sampleDepth(pixelI + offsetX);
    let depthNegY = sampleDepth(pixelI - offsetY);
    let depthPosY = sampleDepth(pixelI + offsetY);
    
    // Choose closer neighbor
    let useNegX = abs(depthNegX - depthCenter) < abs(depthPosX - depthCenter);
    let useNegY = abs(depthNegY - depthCenter) < abs(depthPosY - depthCenter);
    
    var depthX = select(depthPosX, depthNegX, useNegX);
    var depthY = select(depthPosY, depthNegY, useNegY);
    var coordX = select(pixelI + offsetX, pixelI - offsetX, useNegX);
    var coordY = select(pixelI + offsetY, pixelI - offsetY, useNegY);
    
    let ndcX = ndcFromPixel(coordX, dimsF);
    let ndcY = ndcFromPixel(coordY, dimsF);
    let posXView = viewPosFromDepth(invProjParams, ndcX, depthX);
    let posYView = viewPosFromDepth(invProjParams, ndcY, depthY);
    
    // Compute normal
    var dx = posXView - posCView;
    var dy = posYView - posCView;
    var n = cross(dx, dy);
    var normal = normalize(n);
    if (useNegX == useNegY) { normal = -normal; }
    
    // Sample textures
    let uvTerrain = terrainUV(posCWorld);
    let albedo = textureSampleLevel(terrainTex, terrainSampler, uvTerrain, 0.0).xyz;
    let lightVisibility = textureSampleLevel(lightmapTex, terrainSampler, uvTerrain, 0.0).x;
    
    // Lighting
    let lightDir = camera.lightDirVS.xyz;
    let diffuse = max(dot(normal, lightDir), 0.0);
    let ambient = 0.30;
    
    // Specular (roughness-based)
    let viewDir = normalize(-posCView);
    let halfVec = normalize(lightDir + viewDir);
    let roughness = 0.6;
    let specPower = max((1.0 - roughness) * 160.0, 8.0);
    let specStrength = mix(0.04, 0.25, 1.0 - roughness);
    let specularTerm = pow(max(dot(normal, halfVec), 0.0), specPower);
    let specular = specStrength * specularTerm * lightVisibility;
    
    let litColor = albedo * (diffuse * lightVisibility + ambient) + specular;
    
    // Fog (capped at 0.7)
    let fogDensity = camera.metrics.w;
    let fogColor = vec3<f32>(0.6, 0.68, 0.76);
    let dist = length(posCView);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.7);
    let finalColor = mix(litColor, fogColor, fogFactor);
    
    return vec4<f32>(finalColor, 1.0);
}
```

---

## 6. mip_generate.wgsl (Max-Height Mips)

### 6.1 Purpose

Generates the max-height mip pyramid used by the ray-caster for hierarchical traversal.

### 6.2 Bindings

```wgsl
@group(0) @binding(0) var srcMip : texture_2d<u32>;
@group(0) @binding(1) var dstMip : texture_storage_2d<r16uint, write>;

struct MipParams {
    srcSize : vec2<u32>,
    dstSize : vec2<u32>,
};

@group(0) @binding(2) var<uniform> params : MipParams;
```

### 6.3 Compute Shader

```wgsl
@compute @workgroup_size(8, 8, 1)
fn cs_generate_mip(@builtin(global_invocation_id) globalId : vec3<u32>) {
    if (globalId.x >= params.dstSize.x || globalId.y >= params.dstSize.y) {
        return;
    }
    
    // Source coordinates (2x2 block)
    let srcX = globalId.x * 2u;
    let srcY = globalId.y * 2u;
    
    // Sample 2x2 block and take maximum
    let h00 = textureLoad(srcMip, vec2<i32>(i32(srcX), i32(srcY)), 0).r;
    let h10 = textureLoad(srcMip, vec2<i32>(i32(srcX + 1u), i32(srcY)), 0).r;
    let h01 = textureLoad(srcMip, vec2<i32>(i32(srcX), i32(srcY + 1u)), 0).r;
    let h11 = textureLoad(srcMip, vec2<i32>(i32(srcX + 1u), i32(srcY + 1u)), 0).r;
    
    let maxHeight = max(max(h00, h10), max(h01, h11));
    
    textureStore(dstMip, vec2<i32>(globalId.xy), vec4<u32>(maxHeight, 0u, 0u, 1u));
}
```

---

## 7. Debug Visualizations

The shaders include commented-out debug outputs:

```wgsl
// Normal visualization
// return vec4<f32>(normal * 0.5 + 0.5, 1.0);

// Specular-only
// return vec4<f32>(specularTerm, specularTerm, specularTerm, 1.0);

// Distance visualization
// return vec4<f32>(dist * 0.001, dist * 0.01, dist * 0.1, 1.0);
```

---

## 8. Shader Loading

### 8.1 Runtime Loading

```cpp
std::string loadShaderSource(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open shader: %s", path.c_str());
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

wgpu::ShaderModule createShaderModule(wgpu::Device device,
                                       const std::string& source,
                                       const char* label) {
    wgpu::ShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.code = source.c_str();
    
    wgpu::ShaderModuleDescriptor desc = {};
    desc.nextInChain = &wgslDesc;
    desc.label = label;
    
    return device.CreateShaderModule(&desc);
}
```
