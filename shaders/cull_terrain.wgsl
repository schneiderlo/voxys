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
};

struct IndirectArgs {
    indexCount : u32,
    instanceCount : atomic<u32>,
    firstIndex : u32,
    baseVertex : u32,
    firstInstance : u32,
};

struct CullUniforms {
    tilesX : u32,
    tilesY : u32,
    totalTiles : u32,
    tileSizeGrid : u32,
    originAndCellScale : vec4<f32>, // x/y = origin, z = cell scale
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var<storage, read_write> indirectArgs : IndirectArgs;
@group(0) @binding(2) var<storage, read_write> visibleIndices : array<u32>;
@group(0) @binding(3) var<uniform> cull : CullUniforms;

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

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) global_id : vec3<u32>) {
    let tileIndex = global_id.x;

    let terrainSize = vec2<u32>(u32(camera.terrainSize.x), u32(camera.terrainSize.y));
    if (tileIndex >= cull.totalTiles) {
        return;
    }

    // Decode tile position
    let tileX = tileIndex % cull.tilesX;
    let tileY = tileIndex / cull.tilesX;

    // Calculate World Space AABB
    // Based on vs() logic in terrain.wgsl

    let heightScale = camera.metrics.x;
    let cellScale = cull.originAndCellScale.z;
    let origin = cull.originAndCellScale.xy;

    let startX = tileX * cull.tileSizeGrid;
    let startY = tileY * cull.tileSizeGrid;
    let endX = min(startX + cull.tileSizeGrid, terrainSize.x);
    let endY = min(startY + cull.tileSizeGrid, terrainSize.y);

    let minX = f32(startX) * cellScale - origin.x;
    let maxX = f32(endX) * cellScale - origin.x;
    let minZ = f32(startY) * cellScale - origin.y;
    let maxZ = f32(endY) * cellScale - origin.y;

    // Y Bounds: conservative [-heightScale, +heightScale]
    let minY = -heightScale;
    let maxY = heightScale;

    let aabbMin = vec3<f32>(minX, minY, minZ);
    let aabbMax = vec3<f32>(maxX, maxY, maxZ);

    if (isAABBVisible(aabbMin, aabbMax)) {
        // Append to visible list
        let outIndex = atomicAdd(&indirectArgs.instanceCount, 1u);
        visibleIndices[outIndex] = tileIndex;
    }
}
