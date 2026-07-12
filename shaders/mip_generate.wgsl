// ═══════════════════════════════════════════════════════════════════════════════
// mip_generate.wgsl - Max-Height Mip Generation Compute Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Generates the max-height mip pyramid used by the ray-caster for hierarchical
// traversal. Each output texel contains the maximum height of a 2×2 block from
// the source mip level.
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
    
    // Source coordinates (2×2 block origin)
    let srcX = globalId.x * 2u;
    let srcY = globalId.y * 2u;
    
    // Clamp to valid source coordinates
    let maxX = params.srcSize.x - 1u;
    let maxY = params.srcSize.y - 1u;
    
    // Sample 2×2 block with edge clamping and take maximum
    let h00 = textureLoad(srcMip, vec2<i32>(i32(min(srcX, maxX)), i32(min(srcY, maxY))), 0).r;
    let h10 = textureLoad(srcMip, vec2<i32>(i32(min(srcX + 1u, maxX)), i32(min(srcY, maxY))), 0).r;
    let h01 = textureLoad(srcMip, vec2<i32>(i32(min(srcX, maxX)), i32(min(srcY + 1u, maxY))), 0).r;
    let h11 = textureLoad(srcMip, vec2<i32>(i32(min(srcX + 1u, maxX)), i32(min(srcY + 1u, maxY))), 0).r;
    
    let maxHeight = max(max(h00, h10), max(h01, h11));
    
    // Write to destination mip level
    textureStore(dstMip, vec2<i32>(globalId.xy), vec4<u32>(maxHeight, 0u, 0u, 1u));
}


