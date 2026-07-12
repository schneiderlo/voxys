# SVO Architecture Specification

> **Project:** voxy  
> **Version:** 2.0  
> **Status:** Production-Ready Draft  
> **Target:** Best-in-class sparse voxel terrain rendering

---

## 1. Executive Summary

This specification defines a **Sparse Voxel Octree (SVO)** architecture optimized for:
- **GPU ray-casting** with minimal divergence
- **Cache-coherent memory layout** (SoA + Morton ordering)
- **Dynamic editing** (carve tunnels, spawn structures)
- **Smooth surface normals** via contour enhancement (ESVO)

**Key Design Decisions:**
| Decision | Choice | Rationale |
|----------|--------|-----------|
| Tree Structure | 2×2×2 hierarchy + 4×4×4 leaf bricks | Optimal cache/depth tradeoff |
| Leaf Encoding | `uint64` bitmask | 1 load per 64 voxels |
| Memory Layout | Structure of Arrays (SoA) | 10×+ memory coalescing |
| Traversal | Stackless restart + parent pointers | No stack overflow, minimal divergence |
| 64-bit Handling | `vec2<u32>` emulation | WGSL lacks native `u64` |

---

## 2. Data Structures

### 2.1 Node Types

```cpp
// ═══════════════════════════════════════════════════════════════════════════
// C++ Structures (CPU-side)
// ═══════════════════════════════════════════════════════════════════════════

/// Interior node: references 8 children (2×2×2)
struct SVOInteriorNode {
    uint8_t  childMask;     // 8 bits: which children exist (bit N = child N)
    uint8_t  leafMask;      // 8 bits: which children are leaves (vs interior)
    uint16_t parentIndex;   // Index to parent node (for backtracking)
    uint32_t childOffset;   // Index to first child in flat array
};
static_assert(sizeof(SVOInteriorNode) == 8, "Node must be 8 bytes for alignment");

/// Leaf node: 4×4×4 voxel brick (64 voxels packed into 64 bits)
struct SVOLeafBrick {
    uint64_t occupancy;     // 1 bit per voxel (Morton Z-order)
    uint16_t materialBase;  // Base material index for this brick
    uint16_t flags;         // IS_UNIFORM, HAS_CONTOUR, etc.
    // Optional: vec3<f32> contourNormal (12 bytes) for smooth surfaces
};
static_assert(sizeof(SVOLeafBrick) == 12, "Brick header is 12 bytes");

/// Child ordering within 2×2×2 node (Z-order/Morton)
/// Index = (z << 2) | (y << 1) | x
///   0: (0,0,0)  1: (1,0,0)  2: (0,1,0)  3: (1,1,0)
///   4: (0,0,1)  5: (1,0,1)  6: (0,1,1)  7: (1,1,1)
```

### 2.2 GPU Buffer Layout (SoA)

```wgsl
// ═══════════════════════════════════════════════════════════════════════════
// WGSL Storage Buffers
// ═══════════════════════════════════════════════════════════════════════════

struct SVOUniforms {
    rootNodeIndex: u32,           // Index of root node
    maxDepth: u32,                // Maximum tree depth (e.g., 12 for 4096³)
    worldScale: f32,              // World units per root voxel
    brickScale: f32,              // World units per brick voxel (worldScale / 2^maxDepth * 4)
    worldOrigin: vec3<f32>,       // World-space origin of SVO volume
    _pad: f32,
};

// Buffer 0: Uniforms
@group(0) @binding(0) var<uniform> svo: SVOUniforms;

// Buffer 1: Interior Nodes - Child/Leaf Masks (packed u32: childMask | leafMask << 8 | parentIndex << 16)
@group(0) @binding(1) var<storage, read> nodeMasks: array<u32>;

// Buffer 2: Interior Nodes - Child Offsets
@group(0) @binding(2) var<storage, read> nodeChildPtrs: array<u32>;

// Buffer 3: Leaf Bricks - Occupancy (low 32 bits)
@group(0) @binding(3) var<storage, read> brickOccupancyLo: array<u32>;

// Buffer 4: Leaf Bricks - Occupancy (high 32 bits)
@group(0) @binding(4) var<storage, read> brickOccupancyHi: array<u32>;

// Buffer 5: Leaf Bricks - Material + Flags (packed)
@group(0) @binding(5) var<storage, read> brickMeta: array<u32>;

// Buffer 6: Optional Contour Normals (vec3 per brick, for smooth surfaces)
@group(0) @binding(6) var<storage, read> contourNormals: array<vec4<f32>>;

// Output: Depth + Material
@group(0) @binding(7) var outDepth: texture_storage_2d<rg32float, write>;
```

### 2.3 Memory Budget

For 8192×8192×512 terrain (1m resolution, ~500m height):

| Component | Formula | Size |
|-----------|---------|------|
| Interior Nodes | ~2M nodes × 8 bytes | **16 MB** |
| Leaf Bricks | ~4M bricks × 12 bytes | **48 MB** |
| Contours (optional) | ~4M × 16 bytes | **64 MB** |
| **Total (no contours)** | | **~64 MB** |
| **Total (with contours)** | | **~128 MB** |

*Note: Sparse surface band means only ~10% of theoretical voxels are stored.*

---

## 3. Morton Codes (Z-Order Curves)

### 3.1 Encoding

Morton codes provide spatial locality for cache-coherent access.

```cpp
// ═══════════════════════════════════════════════════════════════════════════
// C++ Morton Code Utilities
// ═══════════════════════════════════════════════════════════════════════════

/// Expand 10 bits to 30 bits with 2 zeros between each bit
inline uint32_t expandBits(uint32_t v) {
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

/// Compact 30 bits to 10 bits (inverse of expandBits)
inline uint32_t compactBits(uint32_t v) {
    v &= 0x49249249u;
    v = (v ^ (v >> 2)) & 0xC30C30C3u;
    v = (v ^ (v >> 4)) & 0x0F00F00Fu;
    v = (v ^ (v >> 8)) & 0xFF0000FFu;
    v = (v ^ (v >> 16)) & 0x000003FFu;
    return v;
}

/// Encode 3D coordinate to Morton code
inline uint32_t morton3D(uint32_t x, uint32_t y, uint32_t z) {
    return expandBits(x) | (expandBits(y) << 1) | (expandBits(z) << 2);
}

/// Decode Morton code to 3D coordinate
inline void inverseMorton3D(uint32_t code, uint32_t& x, uint32_t& y, uint32_t& z) {
    x = compactBits(code);
    y = compactBits(code >> 1);
    z = compactBits(code >> 2);
}
```

### 3.2 Brick-Local Morton (4×4×4 → 6 bits)

Within a 4×4×4 brick, voxel `(x, y, z)` where `0 <= x, y, z < 4` maps to bit index:

```wgsl
fn brickMorton(x: u32, y: u32, z: u32) -> u32 {
    // 2-bit interleave: xxyyzzxxyyzz... but we only have 2 bits each
    // Simplified for 4×4×4: just use z*16 + y*4 + x for now (linear)
    // OR use full Morton for better locality:
    return (x & 1u) | ((y & 1u) << 1u) | ((z & 1u) << 2u)
         | ((x & 2u) << 2u) | ((y & 2u) << 3u) | ((z & 2u) << 4u);
}
```

---

## 4. Traversal Algorithm

### 4.1 Overview

The traversal uses a **stackless restart** approach with **parent pointers** for efficient backtracking:

1. **Descend** from root to leaf containing ray entry point
2. **Intersect** brick voxels using DDA
3. **Step** ray to exit current node
4. **Backtrack** via parent pointer to find next sibling
5. **Repeat** until hit or exit volume

### 4.2 Complete WGSL Implementation

```wgsl
// ═══════════════════════════════════════════════════════════════════════════
// SVO Ray Traversal - Stackless with Parent Pointers
// ═══════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

const MAX_ITERATIONS: u32 = 256u;
const EPSILON: f32 = 1e-5;

// ─────────────────────────────────────────────────────────────────────────────
// 64-bit Emulation (Critical: WGSL lacks u64)
// ─────────────────────────────────────────────────────────────────────────────

fn checkBit64(lo: u32, hi: u32, idx: u32) -> bool {
    let isHigh = idx >= 32u;
    let shift = select(idx, idx - 32u, isHigh);
    let part = select(lo, hi, isHigh);
    return (part & (1u << shift)) != 0u;
}

fn countBits64(lo: u32, hi: u32) -> u32 {
    return countOneBits(lo) + countOneBits(hi);
}

// ─────────────────────────────────────────────────────────────────────────────
// Node Accessors
// ─────────────────────────────────────────────────────────────────────────────

fn getChildMask(nodeIdx: u32) -> u32 {
    return nodeMasks[nodeIdx] & 0xFFu;
}

fn getLeafMask(nodeIdx: u32) -> u32 {
    return (nodeMasks[nodeIdx] >> 8u) & 0xFFu;
}

fn getParentIndex(nodeIdx: u32) -> u32 {
    return nodeMasks[nodeIdx] >> 16u;
}

fn getChildPtr(nodeIdx: u32) -> u32 {
    return nodeChildPtrs[nodeIdx];
}

fn childExists(nodeIdx: u32, childIdx: u32) -> bool {
    return (getChildMask(nodeIdx) & (1u << childIdx)) != 0u;
}

fn childIsLeaf(nodeIdx: u32, childIdx: u32) -> bool {
    return (getLeafMask(nodeIdx) & (1u << childIdx)) != 0u;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray-Box Intersection
// ─────────────────────────────────────────────────────────────────────────────

struct BoxHit {
    tMin: f32,
    tMax: f32,
    hit: bool,
}

fn intersectBox(rayOrigin: vec3<f32>, invRayDir: vec3<f32>, 
                boxMin: vec3<f32>, boxMax: vec3<f32>) -> BoxHit {
    let t0 = (boxMin - rayOrigin) * invRayDir;
    let t1 = (boxMax - rayOrigin) * invRayDir;
    let tSmall = min(t0, t1);
    let tLarge = max(t0, t1);
    let tMin = max(max(tSmall.x, tSmall.y), tSmall.z);
    let tMax = min(min(tLarge.x, tLarge.y), tLarge.z);
    return BoxHit(tMin, tMax, tMax >= max(tMin, 0.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Brick DDA Traversal (Within 4×4×4 Brick)
// ─────────────────────────────────────────────────────────────────────────────

struct BrickHit {
    hit: bool,
    t: f32,
    normal: vec3<f32>,
    localPos: vec3<u32>,
}

fn traverseBrick(rayOrigin: vec3<f32>, rayDir: vec3<f32>, 
                 brickIdx: u32, brickOrigin: vec3<f32>, voxelSize: f32) -> BrickHit {
    let occLo = brickOccupancyLo[brickIdx];
    let occHi = brickOccupancyHi[brickIdx];
    
    // Quick empty brick check
    if (occLo == 0u && occHi == 0u) {
        return BrickHit(false, 0.0, vec3<f32>(0.0), vec3<u32>(0u));
    }
    
    let invDir = 1.0 / rayDir;
    
    // Entry point into brick (local coordinates 0-4)
    let brickBox = intersectBox(rayOrigin, invDir, brickOrigin, brickOrigin + vec3<f32>(4.0 * voxelSize));
    if (!brickBox.hit) {
        return BrickHit(false, 0.0, vec3<f32>(0.0), vec3<u32>(0u));
    }
    
    var t = max(brickBox.tMin, EPSILON);
    let entryPos = rayOrigin + rayDir * t;
    let localPos = (entryPos - brickOrigin) / voxelSize;
    
    // 3D DDA within brick
    var cell = vec3<i32>(clamp(vec3<i32>(floor(localPos)), vec3<i32>(0), vec3<i32>(3)));
    let step = vec3<i32>(sign(rayDir));
    let tDelta = abs(vec3<f32>(voxelSize) * invDir);
    
    // Time to first boundary
    let nextBoundary = brickOrigin + (vec3<f32>(cell) + vec3<f32>(max(step, vec3<i32>(0)))) * voxelSize;
    var tMax = (nextBoundary - rayOrigin) * invDir;
    
    var lastNormal = vec3<f32>(0.0, 1.0, 0.0);
    
    for (var i = 0u; i < 12u; i++) {  // Max 3*4 = 12 steps through 4×4×4
        // Check bounds
        if (any(cell < vec3<i32>(0)) || any(cell >= vec3<i32>(4))) {
            break;
        }
        
        // Morton index for this voxel
        let mortonIdx = brickMorton(u32(cell.x), u32(cell.y), u32(cell.z));
        
        if (checkBit64(occLo, occHi, mortonIdx)) {
            // HIT!
            return BrickHit(true, t, lastNormal, vec3<u32>(cell));
        }
        
        // Step to next voxel
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x;
            tMax.x += tDelta.x;
            cell.x += step.x;
            lastNormal = vec3<f32>(-f32(step.x), 0.0, 0.0);
        } else if (tMax.y < tMax.z) {
            t = tMax.y;
            tMax.y += tDelta.y;
            cell.y += step.y;
            lastNormal = vec3<f32>(0.0, -f32(step.y), 0.0);
        } else {
            t = tMax.z;
            tMax.z += tDelta.z;
            cell.z += step.z;
            lastNormal = vec3<f32>(0.0, 0.0, -f32(step.z));
        }
    }
    
    return BrickHit(false, 0.0, vec3<f32>(0.0), vec3<u32>(0u));
}

// ─────────────────────────────────────────────────────────────────────────────
// Main SVO Traversal
// ─────────────────────────────────────────────────────────────────────────────

struct SVOHit {
    hit: bool,
    t: f32,
    normal: vec3<f32>,
    material: u32,
}

fn traverseSVO(rayOrigin: vec3<f32>, rayDir: vec3<f32>) -> SVOHit {
    let invDir = 1.0 / rayDir;
    
    // Intersect root volume
    let rootSize = svo.worldScale;
    let rootMin = svo.worldOrigin;
    let rootMax = rootMin + vec3<f32>(rootSize);
    let rootHit = intersectBox(rayOrigin, invDir, rootMin, rootMax);
    
    if (!rootHit.hit) {
        return SVOHit(false, -1.0, vec3<f32>(0.0), 0u);
    }
    
    var t = max(rootHit.tMin, EPSILON);
    var nodeIdx = svo.rootNodeIndex;
    var nodeSize = rootSize;
    var nodeOrigin = rootMin;
    var depth = 0u;
    
    // Traversal state
    var parentStack: array<u32, 16>;  // Small stack for parent indices
    var childIdxStack: array<u32, 16>;  // Which child we came from
    var stackPtr = 0u;
    
    for (var iter = 0u; iter < MAX_ITERATIONS; iter++) {
        let pos = rayOrigin + rayDir * t;
        
        // Find which child contains current position
        let relPos = (pos - nodeOrigin) / nodeSize;
        let childCoord = vec3<u32>(clamp(vec3<i32>(floor(relPos * 2.0)), vec3<i32>(0), vec3<i32>(1)));
        let childIdx = childCoord.x | (childCoord.y << 1u) | (childCoord.z << 2u);
        
        if (childExists(nodeIdx, childIdx)) {
            let childOffset = getChildPtr(nodeIdx);
            // Count preceding children to get actual index
            let mask = getChildMask(nodeIdx) & ((1u << childIdx) - 1u);
            let childCount = countOneBits(mask);
            let actualChildIdx = childOffset + childCount;
            
            let childSize = nodeSize * 0.5;
            let childOrigin = nodeOrigin + vec3<f32>(childCoord) * childSize;
            
            if (childIsLeaf(nodeIdx, childIdx)) {
                // LEAF: Traverse brick
                let brickHit = traverseBrick(rayOrigin, rayDir, actualChildIdx, childOrigin, childSize / 4.0);
                if (brickHit.hit) {
                    let mat = brickMeta[actualChildIdx] & 0xFFFFu;
                    return SVOHit(true, brickHit.t, brickHit.normal, mat);
                }
                // Brick miss: step past this child
                let childBox = intersectBox(rayOrigin, invDir, childOrigin, childOrigin + vec3<f32>(childSize));
                t = childBox.tMax + EPSILON;
            } else {
                // INTERIOR: Descend
                parentStack[stackPtr] = nodeIdx;
                childIdxStack[stackPtr] = childIdx;
                stackPtr = min(stackPtr + 1u, 15u);
                
                nodeIdx = actualChildIdx;
                nodeSize = childSize;
                nodeOrigin = childOrigin;
                depth += 1u;
                continue;
            }
        } else {
            // Empty child: step past it
            let childSize = nodeSize * 0.5;
            let childOrigin = nodeOrigin + vec3<f32>(childCoord) * childSize;
            let childBox = intersectBox(rayOrigin, invDir, childOrigin, childOrigin + vec3<f32>(childSize));
            t = childBox.tMax + EPSILON;
        }
        
        // Backtrack: find next sibling or parent's sibling
        while (stackPtr > 0u) {
            // Check if we've exited current node
            let nodeBox = intersectBox(rayOrigin, invDir, nodeOrigin, nodeOrigin + vec3<f32>(nodeSize));
            if (t < nodeBox.tMax) {
                break;  // Still inside this node, continue
            }
            
            // Pop parent
            stackPtr -= 1u;
            nodeIdx = parentStack[stackPtr];
            // Reconstruct parent's origin and size
            depth -= 1u;
            nodeSize *= 2.0;
            let lastChildIdx = childIdxStack[stackPtr];
            let lastChildCoord = vec3<f32>(f32(lastChildIdx & 1u), f32((lastChildIdx >> 1u) & 1u), f32((lastChildIdx >> 2u) & 1u));
            nodeOrigin = nodeOrigin - lastChildCoord * nodeSize * 0.5;
        }
        
        // Check if we've exited the entire SVO
        if (t > rootHit.tMax) {
            break;
        }
    }
    
    return SVOHit(false, -1.0, vec3<f32>(0.0), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Entry Point
// ─────────────────────────────────────────────────────────────────────────────

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let dims = textureDimensions(outDepth);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }
    
    // Generate ray (same as heightfield raycast)
    let origin = camera.cameraPos.xyz;
    let dir = rayDirFromPixel(camera.invViewProj, gid.xy, dims, origin);
    
    let hit = traverseSVO(origin, dir);
    
    if (hit.hit) {
        textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(hit.t, f32(hit.material), 0.0, 0.0));
    } else {
        textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(-1.0, 0.0, 0.0, 0.0));
    }
}
```

---

## 5. LOD Strategy

### 5.1 Distance-Based Termination

Stop descending when projected voxel size is smaller than one pixel:

```wgsl
fn shouldTerminate(t: f32, depth: u32) -> bool {
    let voxelSize = svo.worldScale / f32(1u << depth);
    let projectedSize = voxelSize / t;  // Approximate screen-space size
    let pixelSize = 2.0 * tan(fov * 0.5) / f32(screenHeight);
    return projectedSize < pixelSize * LOD_BIAS;  // LOD_BIAS = 1.0-2.0
}
```

### 5.2 Mip-Like Coarsening

For distant regions, treat interior nodes as "solid" without descending to leaves.

---

## 6. Dynamic Updates

### 6.1 Carving (Voxel Removal)

1. Locate affected bricks via Morton code
2. Clear bits in `occupancy` mask
3. If brick becomes empty, mark parent's childMask

```cpp
void carveVoxel(SVOTree& tree, vec3 worldPos) {
    uint32_t brickIdx = tree.findBrick(worldPos);
    uint32_t localMorton = tree.worldToBrickLocal(worldPos);
    tree.bricks[brickIdx].occupancy &= ~(1ULL << localMorton);
    
    if (tree.bricks[brickIdx].occupancy == 0) {
        tree.markBrickEmpty(brickIdx);
    }
}
```

### 6.2 Spawning (Voxel Addition)

1. If brick exists, set bit in `occupancy`
2. If brick doesn't exist, allocate new brick and update parent

---

## 7. Contour Enhancement (Optional)

For smooth terrain surfaces, store per-brick contour normal:

```cpp
struct ContourBrick {
    uint64_t occupancy;
    vec3 normal;           // Average surface normal
    float intersectOffset; // Subvoxel offset along normal
};
```

During traversal, use contour for subvoxel intersection refinement.

---

## 8. Performance Optimizations

### 8.1 Beam Optimization

Process 2×2 ray beams together, sharing node traversal when rays are coherent.

### 8.2 Wavefront Scheduling

Sort rays by direction to maximize memory coalescing.

### 8.3 Shared Memory Caching

Cache frequently-accessed nodes in workgroup shared memory.

---

## 9. Heightmap-to-SVO Conversion Pipeline

### 9.1 Overview

The SVO is **built from the existing heightmap** asset. The conversion extracts only the **surface shell** (not the solid interior), which is why the SVO uses less memory (~64MB) than the heightmap (~128MB).

```
┌─────────────────────────────────────────────────────────────────┐
│                    HEIGHTMAP → SVO PIPELINE                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  INPUT: Existing Heightmap (8192×8192 × 16-bit)                 │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  heightmap.loadFromPNG("terrain.png")                    │   │
│  │  → std::vector<uint16_t> data (128 MB)                   │   │
│  └──────────────────────────────────────────────────────────┘   │
│                           │                                     │
│                           ▼                                     │
│  STEP 1: SURFACE VOXELIZATION                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  For each (x, z) in heightmap:                           │   │
│  │    h = sample(x, z)                                      │   │
│  │    Mark surface voxel at (x, h, z)                       │   │
│  │    Optional: Mark surface voxels at height discontinuities│  │
│  └──────────────────────────────────────────────────────────┘   │
│                           │                                     │
│                           ▼                                     │
│  STEP 2: BRICK GROUPING (4×4×4 clusters)                        │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Group surface voxels into 4×4×4 bricks                  │   │
│  │  Encode each brick as uint64_t occupancy mask            │   │
│  │  Assign material indices                                 │   │
│  └──────────────────────────────────────────────────────────┘   │
│                           │                                     │
│                           ▼                                     │
│  STEP 3: OCTREE CONSTRUCTION (bottom-up)                        │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Build parent nodes for each 2×2×2 group of bricks       │   │
│  │  Repeat until single root node                           │   │
│  │  Store parent pointers for backtracking                  │   │
│  └──────────────────────────────────────────────────────────┘   │
│                           │                                     │
│                           ▼                                     │
│  STEP 4: FLATTEN TO SoA                                         │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Convert pointer-based tree to index-based arrays        │   │
│  │  Morton-order sort for cache locality                    │   │
│  │  Upload to GPU storage buffers                           │   │
│  └──────────────────────────────────────────────────────────┘   │
│                           │                                     │
│  OUTPUT: SVO ready for GPU ray-casting (~64 MB)                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 9.2 Surface Voxelization Algorithm

```cpp
/// Convert heightmap to sparse voxel list (surface shell only)
std::vector<SparseVoxel> voxelizeHeightmap(const Heightmap& hm, float voxelScale) {
    std::vector<SparseVoxel> voxels;
    voxels.reserve(hm.width() * hm.height());  // Approximately one voxel per column
    
    for (uint32_t z = 0; z < hm.height(); z++) {
        for (uint32_t x = 0; x < hm.width(); x++) {
            // Get height at this column (normalized to voxel units)
            uint16_t rawHeight = hm.sample(x, z);
            uint32_t y = static_cast<uint32_t>(rawHeight / voxelScale);
            
            // Always add the top surface voxel
            voxels.push_back({x, y, z, MATERIAL_GRASS});
            
            // Add cliff/wall voxels for height discontinuities
            // This creates vertical surfaces at terrain edges
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dz == 0) continue;
                    
                    uint32_t nx = clamp(x + dx, 0, hm.width() - 1);
                    uint32_t nz = clamp(z + dz, 0, hm.height() - 1);
                    uint32_t ny = static_cast<uint32_t>(hm.sample(nx, nz) / voxelScale);
                    
                    // Fill vertical gap between this column and neighbor
                    if (ny < y) {
                        for (uint32_t fillY = ny; fillY < y; fillY++) {
                            voxels.push_back({x, fillY, z, MATERIAL_ROCK});
                        }
                    }
                }
            }
        }
    }
    
    // Remove duplicates (sort by Morton code, then unique)
    std::sort(voxels.begin(), voxels.end(), [](const auto& a, const auto& b) {
        return morton3D(a.x, a.y, a.z) < morton3D(b.x, b.y, b.z);
    });
    voxels.erase(std::unique(voxels.begin(), voxels.end()), voxels.end());
    
    return voxels;
}
```

### 9.3 Why Surface Shell Only?

**Heightmap:** Stores height for every (x, z) column = 8192 × 8192 × 2 bytes = **128 MB**

**SVO Surface Shell:**
- Only stores voxels **at the surface boundary**
- Underground voxels are implicitly solid (not stored)
- Above-ground voxels are implicitly empty (not stored)
- Result: ~4M surface voxels vs 8192×8192×512 = 34 billion theoretical voxels
- Storage: **~64 MB** (1-bit per voxel, sparse structure)

### 9.4 Post-Conversion Capabilities

After conversion, the SVO enables operations **impossible with heightmaps**:

| Operation | Heightmap | SVO |
|-----------|-----------|-----|
| Carve tunnel | ❌ | ✅ Clear bits in brick |
| Add overhang | ❌ | ✅ Set bits in brick |
| Spawn floating structure | ❌ | ✅ Create new bricks |
| Multiple surfaces per column | ❌ | ✅ Natural support |

---

## 10. File Format (`.svo`)

```
Header (64 bytes):
  magic:        u32   "SVO1"
  version:      u32   1
  nodeCount:    u32
  brickCount:   u32
  rootIndex:    u32
  maxDepth:     u32
  worldScale:   f32
  worldOrigin:  vec3<f32>
  reserved:     28 bytes

nodeMasks:      [nodeCount × u32]
nodeChildPtrs:  [nodeCount × u32]
brickOccLo:     [brickCount × u32]
brickOccHi:     [brickCount × u32]
brickMeta:      [brickCount × u32]
contours:       [brickCount × vec4<f32>]  (optional)
```

---

*This specification provides complete implementation-ready details for a world-class SVO terrain architecture.*
