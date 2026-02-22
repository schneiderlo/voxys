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

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var<storage, read_write> indirectArgs : IndirectArgs;
@group(0) @binding(2) var<storage, read_write> visibleIndices : array<u32>;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

const TILE_QUADS : u32 = 64u;

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

    // We need to reconstruct the logic for "Total Tiles" to bounds check
    // This duplicates logic from TrianglePath::calculateTileCount()
    // but we can assume we dispatch exactly enough or slightly more.
    // Ideally we'd pass totalTileCount as a uniform, but we can compute it from metrics.

    let step = max(u32(camera.metrics.z), 1u);
    let terrainSize = vec2<u32>(u32(camera.terrainSize.x), u32(camera.terrainSize.y));

    let quadsX = max(((terrainSize.x - 1u) + step - 1u) / step, 1u);
    let quadsY = max(((terrainSize.y - 1u) + step - 1u) / step, 1u);
    let tilesX = max((quadsX + TILE_QUADS - 1u) / TILE_QUADS, 1u);
    let tilesY = max((quadsY + TILE_QUADS - 1u) / TILE_QUADS, 1u);
    let totalTiles = tilesX * tilesY;

    if (tileIndex >= totalTiles) {
        return;
    }

    // Decode tile position
    let tileX = tileIndex % tilesX;
    let tileY = tileIndex / tilesX;

    // Calculate World Space AABB
    // Based on vs() logic in terrain.wgsl

    let heightScale = camera.metrics.x;
    let cellScale = camera.metrics.y;

    // Tile covers TILE_QUADS * step units in heightmap grid space
    let tileSizeGrid = TILE_QUADS * step;

    let startX = tileX * tileSizeGrid;
    let startY = tileY * tileSizeGrid;
    let endX = min(startX + tileSizeGrid, terrainSize.x);
    let endY = min(startY + tileSizeGrid, terrainSize.y);

    // Origin calculation matches Vertex Shader
    let origin = 0.5 * (vec2<f32>(camera.terrainSize) - vec2<f32>(1.0, 1.0)) * cellScale;

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
