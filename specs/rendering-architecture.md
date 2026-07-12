# Rendering Architecture Specification

> **Project:** voxy  
> **Version:** 1.0  
> **Status:** Draft

---

## Overview

The voxy rendering system implements a hybrid terrain rendering architecture with three primary GPU stages. This design provides both a robust baseline (triangle mesh) and an advanced ray-casting path optimized for large heightfield terrains.

---

## 1. Triangle Terrain Path (Baseline)

The triangle terrain path serves as:
- A **debug and comparison baseline** for validating the ray-caster
- A **fallback** for platforms where compute shaders or advanced WebGPU features are problematic

### 1.1 Grid Structure

| Parameter | Value | Description |
|-----------|-------|-------------|
| Heightmap Size | 8192 × 8192 | Total grid dimensions |
| Tile Size | 64 × 64 quads | Quads per instanced tile |
| Tile Count | 128 × 128 | Total tiles covering terrain |

### 1.2 Vertex Shader

**Inputs:**
- `heightTex`: `texture_2d<u32>` or `texture_2d<f32>` (base mip level)
- `cellScale`: World-space size of each heightmap cell
- `heightScale`: Vertical scale factor for height values
- `terrainSize`: Total terrain dimensions in cells

**Processing:**
```
1. Convert vertex index (i, j) to heightmap coordinates
2. Sample height from texture: h = textureLoad(heightTex, coords, 0)
3. Normalize height: h_norm = (h / 65535.0) * 2.0 - 1.0
4. Apply scale: world_y = h_norm * heightScale
5. Compute world XZ: 
   world_x = (i - terrainSize.x * 0.5) * cellScale
   world_z = (j - terrainSize.y * 0.5) * cellScale
6. Compute normals via central differences:
   h_left  = sample(i-1, j)
   h_right = sample(i+1, j)
   h_up    = sample(i, j-1)
   h_down  = sample(i, j+1)
   dx = (h_right - h_left) * heightScale / (2.0 * cellScale)
   dz = (h_down - h_up) * heightScale / (2.0 * cellScale)
   normal = normalize(vec3(-dx, 1.0, -dz))
```

**Outputs:**
- `worldPos`: `vec4<f32>` - World-space vertex position
- `worldNormal`: `vec3<f32>` - World-space surface normal
- `uv`: `vec2<f32>` - Texture coordinates for terrain sampling

### 1.3 Fragment Shader

**Lighting Model:**
- **Diffuse:** Lambertian with configurable sun direction
- **Color:** Height-based gradient or terrain texture sampling
- **Fog:** Exponential distance fog

**Parameters:**
| Uniform | Type | Description |
|---------|------|-------------|
| `sunDirection` | `vec3<f32>` | Normalized light direction |
| `sunColor` | `vec3<f32>` | Light color/intensity |
| `ambientColor` | `vec3<f32>` | Ambient lighting |
| `fogColor` | `vec3<f32>` | Fog color (typically sky color) |
| `fogDensity` | `f32` | Exponential fog density |
| `cameraPos` | `vec3<f32>` | Camera world position |

**Fog Calculation:**
```wgsl
let distance = length(worldPos.xyz - cameraPos);
let fogFactor = exp(-fogDensity * distance);
finalColor = mix(fogColor, shadedColor, fogFactor);
```

---

## 2. Compute Heightfield Ray-Caster (Main Path)

The compute ray-caster is the primary rendering path, optimized for large terrains using hierarchical traversal.

### 2.1 Algorithm Overview

The ray-caster implements a modified **Amanatides & Woo DDA** algorithm operating in 2D (XZ plane) with hierarchical mip-level traversal for efficient empty-space skipping.

### 2.2 Inputs

| Binding | Type | Description |
|---------|------|-------------|
| `heightTex` | `texture_2d<u32>` | Heightmap with max-height mip pyramid |
| `outDepth` | `texture_storage_2d<r32float, write>` | Output depth buffer |

**Camera Uniforms:**

All parameters are packed into a single unified `CameraUniforms` struct (see Section 4 for full layout):

```wgsl
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
};
```

### 2.3 Output Encoding

The `outDepth` texture stores:
- **`t > 0`**: Linear ray distance from camera origin to terrain intersection
- **`t < 0`**: Sky (no terrain intersection); specifically `-1.0` indicates miss

### 2.4 Ray Generation

```wgsl
// Per-pixel ray generation
let pixelCoord = vec2<f32>(globalId.xy) + 0.5;
let ndc = vec4(
    (pixelCoord.x / screenSize.x) * 2.0 - 1.0,
    1.0 - (pixelCoord.y / screenSize.y) * 2.0,  // Y-flip for NDC
    1.0,  // Far plane
    1.0
);

let worldFar = uniforms.invViewProj * ndc;
let worldTarget = worldFar.xyz / worldFar.w;
let rayOrigin = uniforms.cameraPos;
let rayDir = normalize(worldTarget - rayOrigin);
```

### 2.5 AABB Intersection

Before DDA traversal, intersect ray with terrain bounding box:

```wgsl
struct AABB {
    min: vec3<f32>,  // (-terrainSize.x/2 * cellScale, minHeight, -terrainSize.y/2 * cellScale)
    max: vec3<f32>,  // (+terrainSize.x/2 * cellScale, maxHeight, +terrainSize.y/2 * cellScale)
};

fn intersectAABB(rayOrigin: vec3<f32>, rayDir: vec3<f32>, aabb: AABB) -> vec2<f32> {
    let invDir = 1.0 / rayDir;
    let t1 = (aabb.min - rayOrigin) * invDir;
    let t2 = (aabb.max - rayOrigin) * invDir;
    let tMin = max(max(min(t1.x, t2.x), min(t1.y, t2.y)), min(t1.z, t2.z));
    let tMax = min(min(max(t1.x, t2.x), max(t1.y, t2.y)), max(t1.z, t2.z));
    return vec2(tMin, tMax);  // Entry and exit distances
}
```

### 2.6 Multilevel DDA Traversal

**Initialization:**
```wgsl
// Start at coarse mip level (e.g., mip 7 for 8192x8192 = 64x64 cells)
var mipLevel: i32 = i32(uniforms.maxMipLevel) - 6;  // Adjust based on desired starting granularity
var cellSize: f32 = uniforms.cellScale * f32(1 << mipLevel);

// Convert entry point to grid coordinates
let entryPoint = rayOrigin + rayDir * tEntry;
var cellX = i32(floor((entryPoint.x + terrainHalfSize.x) / cellSize));
var cellZ = i32(floor((entryPoint.z + terrainHalfSize.y) / cellSize));

// DDA step directions
let stepX = select(-1, 1, rayDir.x >= 0.0);
let stepZ = select(-1, 1, rayDir.z >= 0.0);

// Time to cross one cell
let tDeltaX = abs(cellSize / rayDir.x);
let tDeltaZ = abs(cellSize / rayDir.z);

// Time to first boundary
var tMaxX = /* compute based on entry point and cell boundaries */;
var tMaxZ = /* compute based on entry point and cell boundaries */;
```

**Main Loop:**
```wgsl
var t = tEntry;
let MAX_ITERATIONS = 2048;

for (var i = 0; i < MAX_ITERATIONS; i++) {
    // Check bounds
    if (cellX < 0 || cellZ < 0 || cellX >= gridSize || cellZ >= gridSize) {
        break;  // Exited terrain
    }
    
    // Sample max height at current mip level
    let maxHeight = textureLoad(heightTex, vec2<i32>(cellX, cellZ), mipLevel).r;
    let maxHeightWorld = (f32(maxHeight) / 65535.0 * 2.0 - 1.0) * uniforms.heightScale;
    
    // Compute ray segment through this cell
    let tNext = min(tMaxX, tMaxZ);
    let rayYMin = rayOrigin.y + rayDir.y * t;
    let rayYMax = rayOrigin.y + rayDir.y * tNext;
    let rayYMinMax = min(rayYMin, rayYMax);
    
    // Check for potential intersection
    if (rayYMinMax <= maxHeightWorld) {
        if (mipLevel == 0) {
            // At finest level - compute exact intersection
            let hitT = computeExactIntersection(t, tNext, cellX, cellZ);
            textureStore(outDepth, globalId.xy, vec4(hitT, 0.0, 0.0, 1.0));
            return;
        } else {
            // Descend to finer mip
            mipLevel -= 1;
            cellSize *= 0.5;
            // Recompute cell coordinates and DDA parameters
            recalculateDDA(&cellX, &cellZ, &tMaxX, &tMaxZ, &tDeltaX, &tDeltaZ, ...);
            continue;
        }
    }
    
    // Distance-based LOD: stop descending if far enough
    let lodDistance = pow(2.0, f32(mipLevel + 4));
    if (t > lodDistance && mipLevel > 0) {
        // Could ascend to coarser mip for faster traversal
        // (Optional optimization)
    }
    
    // Step to next cell
    if (tMaxX < tMaxZ) {
        t = tMaxX;
        tMaxX += tDeltaX;
        cellX += stepX;
    } else {
        t = tMaxZ;
        tMaxZ += tDeltaZ;
        cellZ += stepZ;
    }
}

// No intersection found
textureStore(outDepth, globalId.xy, vec4(-1.0, 0.0, 0.0, 1.0));
```

### 2.7 Distance-Based LOD Heuristic

The ray-caster uses a logarithmic LOD heuristic to balance quality and performance:

**LOD Termination Condition:**
```wgsl
// Stop descending when mip level is fine enough for the distance
if (f32(mipLevel) <= max(log(t) - 6.0, 0.0)) {
    break;  // Accept current hit
}
```

**Level-Up Condition (Ascending):**
```wgsl
// Ascend to coarser mip when far from terrain surface
let levelUpHeight = select(65535.0, f32(128 << mipLevel), mipLevel < 7);
if (yExit - levelUpHeight > h) {
    mipLevel++;
    // Adjust DDA parameters for coarser grid...
}
```

### 2.8 Heightmap Coordinate Space

For efficiency, Y comparisons are done in heightmap coordinate space (0-65535 range):

```wgsl
fn toHeightmapCoordinate(height : f32) -> f32 {
    let normalized = (height / heightScale) * 0.5 + 0.5;
    return normalized * 65535.0;
}

fn toHeightmapScale(height : f32) -> f32 {
    let normalized = (height / heightScale) * 0.5;
    return normalized * 65535.0;
}

// Usage in DDA loop:
let slopeY = toHeightmapScale(dir.y);
let originY = toHeightmapCoordinate(origin.y);

// Compare ray Y (scaled) against heightmap sample (no conversion needed)
let h = textureLoad(heightTex, cell, mipLevel).x - originY;
if (min(yEnter, yExit) <= h) { /* potential hit */ }
```

---

## 3. Fullscreen Blit / Lighting Pass

The final compositing pass reads the ray-caster output and produces the shaded image.

### 3.1 Input Bindings

| Binding | Type | Description |
|---------|------|-------------|
| `depthTex` | `texture_2d<f32>` | Ray distance from compute pass |
| `terrainTex` | `texture_2d<f32>` | Terrain albedo/color texture |
| `lightmapTex` | `texture_2d<f32>` | Precomputed AO/sky visibility |

### 3.2 Sky Rendering

Pixels with `depth < 0` are rendered as sky:

```wgsl
fn renderSky(rayDir: vec3<f32>) -> vec3<f32> {
    // Vertical gradient based on ray direction
    let t = rayDir.y * 0.5 + 0.5;  // Map [-1, 1] to [0, 1]
    let smoothT = t * t * (3.0 - 2.0 * t);  // S-curve
    return mix(uniforms.skyBottom, uniforms.skyTop, smoothT);
}
```

### 3.3 Position Reconstruction

```wgsl
// From depth and screen position
fn reconstructPosition(pixelCoord: vec2<f32>, depth: f32) -> vec3<f32> {
    let ndc = vec2(
        (pixelCoord.x / screenSize.x) * 2.0 - 1.0,
        1.0 - (pixelCoord.y / screenSize.y) * 2.0
    );
    
    // View-space ray direction
    let viewDir = vec3(
        ndc.x * uniforms.invProjParams.x,
        ndc.y * uniforms.invProjParams.y,
        -1.0
    );
    
    // View-space position
    let viewPos = normalize(viewDir) * depth;
    
    // World-space position
    let worldPos = (uniforms.invView * vec4(viewPos, 1.0)).xyz;
    return worldPos;
}
```

### 3.4 Screen-Space Normal Reconstruction

The normal reconstruction picks the closer neighbor on each axis and applies a flip rule:

```wgsl
fn reconstructNormal(pixelCoord: vec2<i32>, centerDepth: f32) -> vec3<f32> {
    // Sample neighbors
    let depthNegX = sampleDepth(pixelCoord - vec2(1, 0));
    let depthPosX = sampleDepth(pixelCoord + vec2(1, 0));
    let depthNegY = sampleDepth(pixelCoord - vec2(0, 1));
    let depthPosY = sampleDepth(pixelCoord + vec2(0, 1));
    
    // Choose closer neighbor for each axis
    let useNegX = abs(depthNegX - centerDepth) < abs(depthPosX - centerDepth);
    let useNegY = abs(depthNegY - centerDepth) < abs(depthPosY - centerDepth);
    
    let depthX = select(depthPosX, depthNegX, useNegX);
    let depthY = select(depthPosY, depthNegY, useNegY);
    
    // Reconstruct view-space positions
    let posC = viewPosFromDepth(invProjParams, ndcCenter, centerDepth);
    let posX = viewPosFromDepth(invProjParams, ndcX, depthX);
    let posY = viewPosFromDepth(invProjParams, ndcY, depthY);
    
    // Compute normal from cross product
    var dx = posX - posC;
    var dy = posY - posC;
    var normal = normalize(cross(dx, dy));
    
    // Flip rule: when both axes use same-sign neighbors, flip normal
    if (useNegX == useNegY) { normal = -normal; }
    
    return normal;
}
```

### 3.5 Terrain UV Computation

```wgsl
fn getTerrainUV(worldPos: vec3<f32>) -> vec2<f32> {
    let terrainHalfSize = uniforms.terrainSize * uniforms.cellScale * 0.5;
    let uv = (worldPos.xz + terrainHalfSize) / (uniforms.terrainSize * uniforms.cellScale);
    return clamp(uv, vec2(0.0), vec2(1.0));
}
```

### 3.6 Final Shading

The shading model uses roughness-based specular and capped fog:

```wgsl
fn shade(viewPos: vec3<f32>, normal: vec3<f32>, 
         albedo: vec3<f32>, lightVisibility: f32) -> vec3<f32> {
    let lightDir = camera.lightDirVS.xyz;
    
    // Diffuse (Lambertian)
    let diffuse = max(dot(normal, lightDir), 0.0);
    
    // Fixed ambient
    let ambient = 0.30;
    
    // Specular (roughness-based Blinn-Phong)
    let viewDir = normalize(-viewPos);
    let halfVec = normalize(lightDir + viewDir);
    let roughness = 0.6;
    let specPower = max((1.0 - roughness) * 160.0, 8.0);  // ~64 for roughness 0.6
    let specStrength = mix(0.04, 0.25, 1.0 - roughness);  // ~0.124 for roughness 0.6
    let specularTerm = pow(max(dot(normal, halfVec), 0.0), specPower);
    let specular = specStrength * specularTerm * lightVisibility;
    
    // Combine lighting with lightmap visibility
    let litColor = albedo * (diffuse * lightVisibility + ambient) + specular;
    
    // Fog (capped at 0.7 for visibility at distance)
    let fogDensity = camera.metrics.w;
    let fogColor = vec3<f32>(0.6, 0.68, 0.76);  // Hardcoded
    let dist = length(viewPos);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.7);
    let finalColor = mix(litColor, fogColor, fogFactor);
    
    return finalColor;
}
```

---

## 4. Uniform Buffer Layout

### 4.1 Unified CameraUniforms

All shaders share a single unified uniform buffer that packs camera, terrain, and lighting parameters:

```wgsl
struct CameraUniforms {
    viewProj : mat4x4<f32>,       // offset: 0,   size: 64 - View-projection matrix
    invViewProj : mat4x4<f32>,    // offset: 64,  size: 64 - Inverse view-projection
    invView : mat4x4<f32>,        // offset: 128, size: 64 - Inverse view matrix
    terrainSize : vec2<f32>,      // offset: 192, size: 8  - Heightmap dimensions
    invTerrainSize : vec2<f32>,   // offset: 200, size: 8  - 1.0 / terrainSize
    metrics : vec4<f32>,          // offset: 208, size: 16 - Packed parameters
    cameraPos : vec4<f32>,        // offset: 224, size: 16 - World-space camera position
    invProjParams : vec4<f32>,    // offset: 240, size: 16 - Inverse projection params
    lightDirVS : vec4<f32>,       // offset: 256, size: 16 - View-space light direction
    // Total: 272 bytes
};
```

### 4.2 Metrics Vector Layout

The `metrics` field packs four scalar parameters into a single `vec4<f32>`:

| Component | Name | Description | Typical Value |
|-----------|------|-------------|---------------|
| `metrics.x` | `heightScale` | World-space height range | 500.0 |
| `metrics.y` | `cellScale` | World-space size per cell | 1.0 |
| `metrics.z` | `step` | LOD step for triangle path | 1, 2, 4, 8... |
| `metrics.w` | `fogDensity` | Exponential fog density | 0.0001 |

### 4.3 C++ Structure

```cpp
struct CameraUniforms {
    glm::mat4 viewProj;           // offset: 0
    glm::mat4 invViewProj;        // offset: 64
    glm::mat4 invView;            // offset: 128
    glm::vec2 terrainSize;        // offset: 192
    glm::vec2 invTerrainSize;     // offset: 200
    glm::vec4 metrics;            // offset: 208  (heightScale, cellScale, step, fogDensity)
    glm::vec4 cameraPos;          // offset: 224
    glm::vec4 invProjParams;      // offset: 240
    glm::vec4 lightDirVS;         // offset: 256
    // Total: 272 bytes (padded to 288 for alignment if needed)
};
```

### 4.4 Hardcoded Values

Some lighting and rendering parameters are hardcoded in the shaders for simplicity:

| Parameter | Value | Location |
|-----------|-------|----------|
| Sky bottom color | `(1.0, 1.0, 0.2)` | ray_blit.wgsl |
| Sky top color | `(0.1, 0.1, 1.0)` | ray_blit.wgsl |
| Fog color | `(0.6, 0.68, 0.76)` | All shaders |
| Fog max factor | `0.7` | All shaders |
| Ambient intensity | `0.30` | ray_blit.wgsl |
| Roughness | `0.6` | ray_blit.wgsl |
| Light direction (triangle) | `(0.3, 0.8, 0.4)` | terrain.wgsl |
| Base terrain color | `(0.1, 0.35, 0.15)` | terrain.wgsl |
| Highlight terrain color | `(0.45, 0.6, 0.35)` | terrain.wgsl |

---

## 5. Pipeline State

### 5.1 Triangle Path Pipeline

```
Vertex Stage:
  - Entry: "vs_terrain"
  - Buffers: None (procedural vertex generation)

Fragment Stage:
  - Entry: "fs_terrain"
  - Targets: [{ format: BGRA8Unorm }]

Primitive:
  - Topology: TriangleList
  - Front Face: CCW
  - Cull Mode: Back

Depth Stencil:
  - Format: Depth32Float
  - Depth Write: Enabled
  - Depth Compare: Less
```

### 5.2 Ray-Cast Compute Pipeline

```
Compute Stage:
  - Entry: "cs_raycast"
  - Workgroup Size: [8, 8, 1]

Dispatch:
  - X: ceil(screenWidth / 8)
  - Y: ceil(screenHeight / 8)
  - Z: 1
```

### 5.3 Blit Pipeline

```
Vertex Stage:
  - Entry: "vs_fullscreen"
  - Buffers: None (fullscreen triangle)

Fragment Stage:
  - Entry: "fs_blit"
  - Targets: [{ format: BGRA8Unorm }]

Primitive:
  - Topology: TriangleList
  - Front Face: CCW
  - Cull Mode: None

Depth Stencil:
  - None (depth already computed)
```

---

## 6. Render Graph

```
Frame Start
    │
    ├─► [Compute Pass: Ray-Cast]
    │       ├── Input: heightTex (read)
    │       └── Output: depthTex (write)
    │
    └─► [Render Pass: Blit/Lighting]
            ├── Input: depthTex, terrainTex, lightmapTex
            └── Output: swapchain image

OR (Triangle Path):

Frame Start
    │
    └─► [Render Pass: Triangle Terrain]
            ├── Input: heightTex
            └── Output: swapchain image + depth buffer
```

---

## 7. Performance Considerations

### 7.1 Ray-Caster Optimizations

1. **Hierarchical Traversal**: Start at coarse mip to skip large empty regions
2. **Distance LOD**: Reduce refinement at distance to save iterations
3. **Early Exit**: Terminate when exiting terrain AABB
4. **Shared Memory**: Cache mip samples within workgroup (future optimization)

### 7.2 Memory Layout

1. **Heightmap Texture**: R16Uint format with full mip chain
2. **Depth Output**: R32Float for precision at distance
3. **Uniform Buffers**: Aligned to 256 bytes for WebGPU requirements

### 7.3 Bandwidth

| Resource | Access Pattern | Notes |
|----------|---------------|-------|
| Heightmap | Random (mip pyramid) | Use texture cache efficiently |
| Depth Output | Write-only, linear | Sequential pixel writes |
| Terrain Texture | Read, UV-based | Standard filtered sampling |
| Lightmap | Read, UV-based | Standard filtered sampling |

---

## 8. Debug Features

- **Mip Level Visualization**: Color-code pixels by final mip level
- **Iteration Count**: Heat map of ray-march iterations
- **Normal Visualization**: Display reconstructed normals as RGB
- **Depth Visualization**: Grayscale depth buffer display
- **Wireframe Mode**: Triangle path with wireframe rendering

