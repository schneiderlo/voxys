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
    waterParams : vec4<f32>,
    waterColorA : vec4<f32>,
    waterColorB : vec4<f32>,
    waterMotion : vec4<f32>,
    lightingColor : vec4<f32>,
    ambientExposure : vec4<f32>,
    fogColor : vec4<f32>,
    waterOptics : vec4<f32>,
    waterFoam : vec4<f32>,
    waterSpectrum : vec4<f32>,
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var rayDepth : texture_2d<f32>;
@group(0) @binding(2) var<storage, read> particles : array<vec4<f32>>;

const PARTICLE_NEAR : f32 = 2.5;
const PARTICLE_FAR : f32 = 65.0;
const PARTICLE_MIN_SIZE : f32 = 0.028;
const PARTICLE_MAX_SIZE : f32 = 0.100;

struct ParticleVertex {
    @builtin(position) position : vec4<f32>,
    @location(0) uv : vec2<f32>,
    @location(1) visibility : f32,
    @location(2) cameraDistance : f32,
};

fn billboardCorner(index : u32) -> vec2<f32> {
    switch index {
        case 0u: { return vec2<f32>(-0.5, -0.5); }
        case 1u: { return vec2<f32>( 0.5, -0.5); }
        case 2u: { return vec2<f32>( 0.5,  0.5); }
        case 3u: { return vec2<f32>(-0.5, -0.5); }
        case 4u: { return vec2<f32>( 0.5,  0.5); }
        default: { return vec2<f32>(-0.5,  0.5); }
    }
}

@vertex
fn vs(@builtin(vertex_index) vertexIndex : u32,
      @builtin(instance_index) instanceIndex : u32) -> ParticleVertex {
    let source = particles[instanceIndex];
    let center = source.xyz;
    let size = mix(PARTICLE_MIN_SIZE, PARTICLE_MAX_SIZE, source.w);
    var forward = camera.cameraPos.xyz - center;
    let cameraDistance = length(forward);
    forward /= max(cameraDistance, 1.0e-5);
    var right = cross(forward, vec3<f32>(0.0, 1.0, 0.0));
    if (dot(right, right) < 1.0e-5) {
        right = vec3<f32>(1.0, 0.0, 0.0);
    } else {
        right = normalize(right);
    }
    let up = normalize(cross(right, forward));
    let corner = billboardCorner(vertexIndex);
    let worldPosition = center + (right * corner.x + up * corner.y) * size;

    let shell = PARTICLE_FAR - PARTICLE_NEAR;
    let normalizedDistance = clamp(
        (cameraDistance - PARTICLE_NEAR) / shell, 0.0, 1.0);
    let distanceFade = 1.0 - normalizedDistance * normalizedDistance;
    let localSurfaceHeight = camera.waterParams.x + camera.waterMotion.y;
    let surfaceFade = 1.0 - smoothstep(
        localSurfaceHeight - 0.2, localSurfaceHeight, center.y);

    var output : ParticleVertex;
    output.position = camera.viewProj * vec4<f32>(worldPosition, 1.0);
    output.uv = corner + vec2<f32>(0.5);
    output.visibility = distanceFade * surfaceFade *
                        select(0.0, 1.0, source.w > 1.0e-6);
    output.cameraDistance = cameraDistance;
    return output;
}

@fragment
fn fs(input : ParticleVertex) -> @location(0) vec4<f32> {
    let dimensions = vec2<i32>(textureDimensions(rayDepth, 0));
    let pixel = clamp(vec2<i32>(floor(input.position.xy)), vec2<i32>(0),
                      dimensions - vec2<i32>(1));
    let sceneDepth = textureLoad(rayDepth, pixel, 0).x;
    if (sceneDepth > 0.0 && sceneDepth + 0.01 < input.cameraDistance) {
        discard;
    }
    let centered = (input.uv - vec2<f32>(0.5)) * 2.0;
    let radialFade = 1.0 - smoothstep(0.5, 1.0, length(centered));
    let alpha = 0.26 * input.visibility * radialFade;
    if (alpha < 0.001) {
        discard;
    }
    return vec4<f32>(vec3<f32>(0.72, 0.88, 0.82), alpha);
}
