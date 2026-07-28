// ═══════════════════════════════════════════════════════════════════════════════
// mip_generate.wgsl - Max-Height Mip Generation Compute Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Generates the max-height mip pyramid used by the ray-caster for hierarchical
// traversal. Each output texel contains the maximum height of its complete
// source footprint. Proportional footprints preserve odd right/bottom edges.
// ═══════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Uniforms
// ─────────────────────────────────────────────────────────────────────────────

struct MipParams {
    srcSize : vec2<u32>,   // Source mip level dimensions
    dstSize : vec2<u32>,   // Destination mip level dimensions
};

// ─────────────────────────────────────────────────────────────────────────────
// Bindings
// ─────────────────────────────────────────────────────────────────────────────

@group(0) @binding(0) var srcMip : texture_2d<u32>;
// Note: r16uint is NOT a valid storage texture format in WebGPU 1.0.
// Only 32-bit formats are supported for storage textures.
// We use r32uint and cast the values appropriately.
@group(0) @binding(1) var dstMip : texture_storage_2d<r32uint, write>;
@group(0) @binding(2) var<uniform> params : MipParams;

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

@compute @workgroup_size(8, 8, 1)
fn cs_generate_mip(@builtin(global_invocation_id) globalId : vec3<u32>) {
    // Bounds check - skip if outside destination texture
    if (globalId.x >= params.dstSize.x || globalId.y >= params.dstSize.y) {
        return;
    }
    
    // A floor-sized mip of an odd source has one footprint wider/taller than
    // 2. Mapping both boundaries proportionally covers every source texel.
    let beginX = globalId.x * params.srcSize.x / params.dstSize.x;
    let endX = (globalId.x + 1u) * params.srcSize.x / params.dstSize.x;
    let beginY = globalId.y * params.srcSize.y / params.dstSize.y;
    let endY = (globalId.y + 1u) * params.srcSize.y / params.dstSize.y;

    var maxHeight = 0u;
    for (var y = beginY; y < endY; y++) {
        for (var x = beginX; x < endX; x++) {
            maxHeight = max(
                maxHeight,
                textureLoad(srcMip, vec2<i32>(i32(x), i32(y)), 0).r);
        }
    }
    
    // Write to destination mip level
    textureStore(dstMip, vec2<i32>(globalId.xy), vec4<u32>(maxHeight, 0u, 0u, 1u));
}

