// ═══════════════════════════════════════════════════════════════════════════════
// decorations.wgsl - Instanced Terrain Decoration Shader
// ═══════════════════════════════════════════════════════════════════════════════

struct CameraUniforms {
    viewProj : mat4x4<f32>,
    invViewProj : mat4x4<f32>,
    invView : mat4x4<f32>,
    terrainSize : vec2<f32>,
    invTerrainSize : vec2<f32>,
    metrics : vec4<f32>,
    cameraPos : vec4<f32>,
    invProjParams : vec4<f32>,
    lightDirVS : vec4<f32>,
    frustumPlanes : array<vec4<f32>, 6>,
    lightDirWS : vec4<f32>,
};

struct TreeInstance {
    baseRadius : vec4<f32>,
    colorHeight : vec4<f32>,
};

struct DecorationParams {
    useRayDepth : u32,
    rayDepthBias : f32,
    fadeStart : f32,
    fadeEnd : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var<storage, read> trees : array<TreeInstance>;
@group(0) @binding(2) var rayDepthTex : texture_2d<f32>;
@group(0) @binding(3) var<uniform> params : DecorationParams;

struct VSOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) worldPos : vec3<f32>,
    @location(1) uv : vec2<f32>,
    @location(2) color : vec3<f32>,
    @location(3) normal : vec3<f32>,
};

fn hash2D(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

@vertex
fn vs(@builtin(vertex_index) vertexIndex : u32,
      @builtin(instance_index) instanceIndex : u32) -> VSOut {
    let tree = trees[instanceIndex];
    let quadIndex = vertexIndex / 6u;
    let localIndex = vertexIndex % 6u;
    var corner = vec2<f32>(-1.0, 0.0);
    if (localIndex == 1u || localIndex == 4u) {
        corner = vec2<f32>(1.0, 0.0);
    }
    if (localIndex == 2u || localIndex == 3u) {
        corner = vec2<f32>(-1.0, 1.0);
    }
    if (localIndex == 5u) {
        corner = vec2<f32>(1.0, 1.0);
    }
    var axis = normalize(vec3<f32>(1.0, 0.0, 1.0));
    if (quadIndex == 1u) {
        axis = normalize(vec3<f32>(-1.0, 0.0, 1.0));
    }

    let base = tree.baseRadius.xyz;
    let radius = tree.baseRadius.w;
    let height = tree.colorHeight.w;
    let worldPos = base + axis * (corner.x * radius) + vec3<f32>(0.0, corner.y * height, 0.0);
    let normal = normalize(vec3<f32>(axis.z, 0.15, -axis.x));

    var out : VSOut;
    out.clipPos = camera.viewProj * vec4<f32>(worldPos, 1.0);
    out.worldPos = worldPos;
    out.uv = corner;
    out.color = tree.colorHeight.rgb;
    out.normal = normal;
    return out;
}

@fragment
fn fs(input : VSOut) -> @location(0) vec4<f32> {
    let dist = length(input.worldPos - camera.cameraPos.xyz);

    if (params.useRayDepth != 0u) {
        let dims = textureDimensions(rayDepthTex, 0);
        let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
        let pixel = clamp(vec2<i32>(floor(input.clipPos.xy)), vec2<i32>(0, 0), maxCoord);
        let terrainDepth = textureLoad(rayDepthTex, pixel, 0).x;
        if (terrainDepth > 0.0 && dist > terrainDepth + params.rayDepthBias) {
            discard;
        }
    }

    let centered = abs(input.uv.x);
    let y = input.uv.y;
    let leafOval = 1.0 - smoothstep(0.52, 1.02, centered + abs(y - 0.60) * 0.72);
    let leafCrown = 1.0 - smoothstep(0.42, 0.92, centered + max(y - 0.78, 0.0) * 1.35);
    let leafLower = smoothstep(0.12, 0.25, y) * (1.0 - smoothstep(0.95, 1.03, y));
    let leafAlpha = max(leafOval, leafCrown) * leafLower;
    let trunkAlpha = (1.0 - smoothstep(0.045, 0.115, centered)) *
                     (1.0 - smoothstep(0.38, 0.64, y));
    let coverage = max(leafAlpha, trunkAlpha);
    if (coverage < 0.09) {
        discard;
    }

    let leafNoise = hash2D(floor(input.worldPos.xz * 1.7 + vec2<f32>(y * 37.0, y * 19.0)));
    var color = input.color * (0.78 + 0.28 * leafNoise);
    let trunkColor = vec3<f32>(0.13, 0.075, 0.035) * (0.82 + 0.26 * leafNoise);
    color = mix(color, trunkColor, smoothstep(0.08, 0.55, trunkAlpha));

    let lightDir = normalize(camera.lightDirWS.xyz);
    let ambient = max(camera.lightDirVS.w, 0.08);
    let diffuse = max(dot(normalize(input.normal), lightDir), 0.0);
    let wrappedDiffuse = 0.30 + 0.70 * diffuse;
    color *= ambient + wrappedDiffuse * 0.62;

    let fade = 1.0 - smoothstep(params.fadeStart, params.fadeEnd, dist);
    if (fade < 0.05) {
        discard;
    }
    color *= 0.72 + 0.28 * fade;

    let fogDensity = max(camera.metrics.w, 0.0);
    let fogColor = vec3<f32>(0.6, 0.68, 0.76);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.72);
    let finalColor = mix(color, fogColor, fogFactor);

    return vec4<f32>(finalColor, 1.0);
}
