// Persistent whitecaps sourced from FFT Jacobian compression.

struct FoamParams {
    deltaTime : f32,
    time : f32,
    padding : vec2<f32>,
};

@group(0) @binding(0) var<uniform> params : FoamParams;
@group(0) @binding(1) var<storage, read> previousFoam : array<f32>;
@group(0) @binding(2) var<storage, read_write> nextFoam : array<f32>;
@group(0) @binding(3) var displacementTexture : texture_2d_array<f32>;
@group(0) @binding(4) var displacementSampler : sampler;
@group(0) @binding(5) var outputFoam : texture_storage_2d<rgba16float, write>;

const RESOLUTION : u32 = 256u;

fn loadFoam(coord : vec2<i32>) -> f32 {
    let wrapped = vec2<u32>(coord) & vec2<u32>(RESOLUTION - 1u);
    return previousFoam[wrapped.y * RESOLUTION + wrapped.x];
}

fn sampleHistory(position : vec2<f32>) -> f32 {
    let base = vec2<i32>(floor(position));
    let f = fract(position);
    let a = mix(loadFoam(base), loadFoam(base + vec2<i32>(1, 0)), f.x);
    let b = mix(loadFoam(base + vec2<i32>(0, 1)),
                loadFoam(base + vec2<i32>(1, 1)), f.x);
    return mix(a, b, f.y);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x >= RESOLUTION || gid.y >= RESOLUTION) { return; }

    // Advect more slowly than the wave phase velocity: foam rides the surface
    // and drifts with the dominant wind/current after a crest has broken.
    let windVelocity = vec2<f32>(1.35, 0.58);
    let cellSize = 96.0 / f32(RESOLUTION);
    let previousPosition = vec2<f32>(gid.xy) -
                           windVelocity * (params.deltaTime / cellSize);
    let history = sampleHistory(previousPosition) * exp(-params.deltaTime * 0.32);

    let uv = (vec2<f32>(gid.xy) + vec2<f32>(0.5)) / f32(RESOLUTION);
    let compression = textureSampleLevel(
        displacementTexture, displacementSampler, uv, 0, 0.0).w;
    let breaking = smoothstep(0.055, 0.22, compression);
    let accumulated = min(1.0, history + breaking * params.deltaTime * 2.4);
    let foam = max(accumulated, breaking * 0.32);

    let index = gid.y * RESOLUTION + gid.x;
    nextFoam[index] = foam;
    textureStore(outputFoam, vec2<i32>(gid.xy), vec4<f32>(foam, 0.0, 0.0, 0.0));
}
