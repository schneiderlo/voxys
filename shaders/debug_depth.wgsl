// ═══════════════════════════════════════════════════════════════════════════════
// debug_depth.wgsl - Debug Depth Visualization Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Simple fullscreen blit shader for visualizing the ray-caster depth output.
// Features:
//   - Fullscreen triangle vertex shader (no vertex buffers)
//   - Depth texture sampling from ray-caster output
//   - Grayscale visualization with configurable range
//   - Sky pixels (negative depth) shown as distinct color
// ═══════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Uniforms
// ─────────────────────────────────────────────────────────────────────────────

struct DebugParams {
    nearDist : f32,      // Near distance for visualization (maps to white)
    farDist : f32,       // Far distance for visualization (maps to black)
    mode : u32,          // 0 = grayscale depth, 1 = normalized depth, 2 = raw depth
    padding : u32,       // Padding for alignment
};

@group(0) @binding(0) var<uniform> params : DebugParams;
@group(0) @binding(1) var depthTex : texture_2d<f32>;

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader - Fullscreen Triangle
// ─────────────────────────────────────────────────────────────────────────────
// Uses a single oversized triangle to cover the screen (more efficient than quad)

struct VSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) uv : vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) i : u32) -> VSOut {
    // Generate fullscreen triangle vertices
    // Vertex 0: (-1, -3) -> uv (0, 2)
    // Vertex 1: (3, 1)   -> uv (2, 0)
    // Vertex 2: (-1, 1)  -> uv (0, 0)
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -3.0),
        vec2<f32>(3.0, 1.0),
        vec2<f32>(-1.0, 1.0)
    );
    var uvs = array<vec2<f32>, 3>(
        vec2<f32>(0.0, 2.0),
        vec2<f32>(2.0, 0.0),
        vec2<f32>(0.0, 0.0)
    );
    
    var out : VSOut;
    out.pos = vec4<f32>(positions[i], 0.0, 1.0);
    out.uv = uvs[i];
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fragment Shader - Depth Visualization
// ─────────────────────────────────────────────────────────────────────────────

@fragment
fn fs(input : VSOut) -> @location(0) vec4<f32> {
    let dims = textureDimensions(depthTex, 0);
    let pixelCoord = vec2<i32>(floor(input.uv * vec2<f32>(dims)));
    
    // Clamp to valid range
    let clampedCoord = clamp(pixelCoord, vec2<i32>(0), vec2<i32>(dims) - vec2<i32>(1));
    
    // Sample depth (R32Float)
    let depth = textureLoad(depthTex, clampedCoord, 0).x;
    
    // Sky (negative depth) - render as sky blue
    if (depth < 0.0) {
        return vec4<f32>(0.4, 0.6, 0.9, 1.0);
    }
    
    // Visualize based on mode
    var color : vec3<f32>;
    
    if (params.mode == 0u) {
        // Mode 0: Grayscale with near/far mapping
        let t = clamp((depth - params.nearDist) / (params.farDist - params.nearDist), 0.0, 1.0);
        // Invert so near is white, far is dark
        let gray = 1.0 - t;
        color = vec3<f32>(gray, gray, gray);
    } else if (params.mode == 1u) {
        // Mode 1: Normalized depth (0-1 range based on near/far)
        let t = clamp((depth - params.nearDist) / (params.farDist - params.nearDist), 0.0, 1.0);
        // Color gradient: white -> yellow -> red -> dark red
        if (t < 0.33) {
            color = mix(vec3<f32>(1.0, 1.0, 1.0), vec3<f32>(1.0, 1.0, 0.0), t * 3.0);
        } else if (t < 0.66) {
            color = mix(vec3<f32>(1.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), (t - 0.33) * 3.0);
        } else {
            color = mix(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.3, 0.0, 0.0), (t - 0.66) * 3.0);
        }
    } else {
        // Mode 2: Raw depth visualization (scaled arbitrarily)
        let scaled = depth * 0.001;  // Assume reasonable terrain distances
        let gray = clamp(1.0 - scaled, 0.0, 1.0);
        color = vec3<f32>(gray, gray, gray);
    }
    
    return vec4<f32>(color, 1.0);
}



