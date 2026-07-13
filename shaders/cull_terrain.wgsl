// ═══════════════════════════════════════════════════════════════════════════════
// cull_terrain.wgsl - Compute Shader for Terrain Frustum Culling
// ═══════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Uniforms & Buffers
// ─────────────────────────────────────────────────────────────────────────────

struct CameraUniforms {
    viewProj : mat4x4<f32>,
    invViewProj : mat4x4<f32>,
    invView : mat4x4<f32>,
    terrainSize : vec2<f32>,
    invTerrainSize : vec2<f32>,
    metrics : vec4<f32>,          // (heightScale, cellScale, step, fogDensity)
    cameraPos : vec4<f32>,
    invProjParams : vec4<f32>,
    lightDirVS : vec4<f32>,
    frustumPlanes : array<vec4<f32>, 6>, // Left, Right, Bottom, Top, Near, Far
    // The trailing fields are unused by culling but must be present so this
    // struct's size matches the C++ CameraUniforms (448 bytes). Strict WebGPU
    // implementations validate the shader-reflected binding size against the
    // buffer/minBindingSize and reject a mismatch.
    lightDirWS : vec4<f32>,
    waterParams : vec4<f32>,
    waterColorA : vec4<f32>,
    waterColorB : vec4<f32>,
    waterMotion : vec4<f32>,
};

struct IndirectArgs {
    indexCount : u32,
    instanceCount : atomic<u32>,
    firstIndex : u32,
    baseVertex : u32,
    firstInstance : u32,
};

struct CullUniforms {
    // Per level: tile count X/Y, candidate-array offset, reserved.
    levels : array<vec4<u32>, 4>,
    // Level 1/2/3 split distances. Level zero is always a leaf.
    splitDistances : vec4<f32>,
    originAndCellScale : vec4<f32>, // x/y = origin, z = cell scale
    // x = total candidate count, y = base step, z = visibility segment size.
    metadata : vec4<u32>,
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var<storage, read_write> indirectArgs : array<IndirectArgs>;
@group(0) @binding(2) var<storage, read_write> visibleIndices : array<u32>;
@group(0) @binding(3) var<uniform> cull : CullUniforms;
@group(0) @binding(4) var<storage, read> tileBounds : array<u32>;

const TILE_QUADS : u32 = 32u;
const LOD_COUNT : u32 = 4u;
const DISTANCE_BIN_COUNT : u32 = 8u;

// ─────────────────────────────────────────────────────────────────────────────
// Culling Logic
// ─────────────────────────────────────────────────────────────────────────────

fn isAABBVisible(minPos: vec3<f32>, maxPos: vec3<f32>) -> bool {
    // Check AABB against all 6 frustum planes
    // Plane is (Nx, Ny, Nz, D). Point is visible if dot(N, P) + D >= 0
    // We use the "center + extents" optimization or just test positive vertex

    // Using center + radius approach
    let center = (minPos + maxPos) * 0.5;
    let extents = (maxPos - minPos) * 0.5;

    for (var i = 0; i < 6; i++) {
        let plane = camera.frustumPlanes[i];
        let normal = plane.xyz;
        let dist = plane.w;

        // Project extents onto plane normal (absolute dot product)
        let r = dot(abs(normal), extents);

        // Distance from center to plane
        let d = dot(normal, center) + dist;

        // If center is behind plane by more than radius, it's outside
        if (d < -r) {
            return false;
        }
    }
    return true;
}

fn levelInfo(level : u32) -> vec4<u32> {
    switch level {
        case 0u: { return cull.levels[0]; }
        case 1u: { return cull.levels[1]; }
        case 2u: { return cull.levels[2]; }
        default: { return cull.levels[3]; }
    }
}

fn splitDistance(level : u32) -> f32 {
    switch level {
        case 1u: { return cull.splitDistances.y; }
        case 2u: { return cull.splitDistances.z; }
        default: { return cull.splitDistances.w; }
    }
}

fn distanceToRect(point : vec2<f32>, rectMin : vec2<f32>, rectMax : vec2<f32>) -> f32 {
    let delta = max(max(rectMin - point, point - rectMax), vec2<f32>(0.0));
    return length(delta);
}

fn nodeRect(tileX : u32, tileY : u32, level : u32) -> vec4<f32> {
    let terrainLast = vec2<u32>(u32(camera.terrainSize.x) - 1u,
                                u32(camera.terrainSize.y) - 1u);
    let step = cull.metadata.y << level;
    let span = TILE_QUADS * step;
    let start = vec2<u32>(tileX, tileY) * span;
    let end = min(start + vec2<u32>(span), terrainLast);
    let cellScale = cull.originAndCellScale.z;
    let origin = cull.originAndCellScale.xy;
    let rectMin = vec2<f32>(start) * cellScale - origin;
    let rectMax = vec2<f32>(end) * cellScale - origin;
    return vec4<f32>(rectMin, rectMax);
}

fn shouldSplit(tileX : u32, tileY : u32, level : u32) -> bool {
    if (level == 0u) { return false; }
    let rect = nodeRect(tileX, tileY, level);
    return distanceToRect(camera.cameraPos.xz, rect.xy, rect.zw) < splitDistance(level);
}

fn reachedFromRoot(tileX : u32, tileY : u32, level : u32) -> bool {
    var ancestor = level + 1u;
    loop {
        if (ancestor >= LOD_COUNT) { break; }
        let shift = ancestor - level;
        if (!shouldSplit(tileX >> shift, tileY >> shift, ancestor)) {
            return false;
        }
        ancestor++;
    }
    return true;
}

fn selectedLevelAt(baseTile : vec2<u32>) -> u32 {
    // WGSL requires every path to return; a `return` inside `loop` cannot be
    // proven exhaustive by the validator, so break out and return once.
    var level = LOD_COUNT - 1u;
    loop {
        if (level == 0u || !shouldSplit(baseTile.x >> level,
                                        baseTile.y >> level,
                                        level)) {
            break;
        }
        level--;
    }
    return level;
}

fn transitionEdgeMask(tileX : u32, tileY : u32, level : u32) -> u32 {
    let base = vec2<u32>(tileX, tileY) << vec2<u32>(level);
    let width = 1u << level;
    let middle = width >> 1u;
    let levelZeroSize = cull.levels[0].xy;
    var mask = 0u;

    // Skirts are needed only on the fine side of a fine/coarse boundary.
    if (base.y > 0u && selectedLevelAt(vec2<u32>(base.x + middle, base.y - 1u)) > level) {
        mask |= 1u;
    }
    if (base.x + width < levelZeroSize.x &&
        selectedLevelAt(vec2<u32>(base.x + width, base.y + middle)) > level) {
        mask |= 2u;
    }
    if (base.y + width < levelZeroSize.y &&
        selectedLevelAt(vec2<u32>(base.x + middle, base.y + width)) > level) {
        mask |= 4u;
    }
    if (base.x > 0u && selectedLevelAt(vec2<u32>(base.x - 1u, base.y + middle)) > level) {
        mask |= 8u;
    }
    return mask;
}

fn distanceBin(distance : f32) -> u32 {
    if (distance < 256.0) { return 0u; }
    if (distance < 512.0) { return 1u; }
    if (distance < 1024.0) { return 2u; }
    if (distance < 2048.0) { return 3u; }
    if (distance < 4096.0) { return 4u; }
    if (distance < 8192.0) { return 5u; }
    if (distance < 16384.0) { return 6u; }
    return 7u;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) global_id : vec3<u32>) {
    let candidateIndex = global_id.x;
    if (candidateIndex >= cull.metadata.x) {
        return;
    }

    var level = 0u;
    if (candidateIndex >= cull.levels[3].z) {
        level = 3u;
    } else if (candidateIndex >= cull.levels[2].z) {
        level = 2u;
    } else if (candidateIndex >= cull.levels[1].z) {
        level = 1u;
    }

    let info = levelInfo(level);
    let localIndex = candidateIndex - info.z;
    let tileX = localIndex % info.x;
    let tileY = localIndex / info.x;

    // A node is emitted only when every ancestor chose to split and this node
    // itself remains a leaf. This independently-evaluated quadtree rule gives
    // complete, non-overlapping terrain coverage without inter-dispatch sync.
    if (!reachedFromRoot(tileX, tileY, level) || shouldSplit(tileX, tileY, level)) {
        return;
    }

    let rect = nodeRect(tileX, tileY, level);
    let packedBounds = tileBounds[candidateIndex];
    let minRaw = f32(packedBounds & 0xffffu);
    let maxRaw = f32(packedBounds >> 16u);
    let heightScale = camera.metrics.x;
    let skirtDepth = max(cull.originAndCellScale.z * f32(cull.metadata.y << level) * 2.0, 2.0);
    let minY = (minRaw / 65535.0 * 2.0 - 1.0) * heightScale - skirtDepth;
    let maxY = (maxRaw / 65535.0 * 2.0 - 1.0) * heightScale;
    let aabbMin = vec3<f32>(rect.x, minY, rect.y);
    let aabbMax = vec3<f32>(rect.z, maxY, rect.w);

    if (!isAABBVisible(aabbMin, aabbMax)) {
        return;
    }

    let distance = distanceToRect(camera.cameraPos.xz, rect.xy, rect.zw);
    let bin = min(distanceBin(distance), DISTANCE_BIN_COUNT - 1u);
    let drawIndex = bin * LOD_COUNT + level;
    let outIndex = atomicAdd(&indirectArgs[drawIndex].instanceCount, 1u);
    let destination = drawIndex * cull.metadata.z + outIndex;

    // x: 10 bits, y: 10 bits, LOD: 2 bits, transition edge mask: 4 bits.
    let edgeMask = transitionEdgeMask(tileX, tileY, level);
    let packedTile = tileX | (tileY << 10u) | (level << 20u) | (edgeMask << 22u);
    visibleIndices[destination] = packedTile;
}
