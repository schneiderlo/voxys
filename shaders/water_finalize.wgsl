// Resolve inverse-FFT fields into displacement and exact normal layers.

struct WaveData {
    height : vec2<f32>,
    displacementX : vec2<f32>,
    displacementZ : vec2<f32>,
    padding : vec2<f32>,
};

struct SimParams {
    time : f32,
    stage : u32,
    axis : u32,
    size : u32,
    patchLengths : vec2<f32>,
    cascadeAmplitudes : vec2<f32>,
    choppiness : f32,
    directionalSineScale : f32,
    padding : vec2<f32>,
};

@group(0) @binding(0) var<storage, read> spatialData : array<WaveData>;
@group(0) @binding(1) var outputTexture : texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(2) var<uniform> params : SimParams;

const RESOLUTION : u32 = 256u;
const CASCADE_COUNT : u32 = 2u;
fn patchLength(cascade : u32) -> f32 {
    return params.patchLengths[cascade];
}

fn cascadeAmplitude(cascade : u32) -> f32 {
    return params.cascadeAmplitudes[cascade];
}

fn loadWave(coord : vec2<u32>, cascade : u32) -> WaveData {
    let wrapped = coord & vec2<u32>(RESOLUTION - 1u);
    return spatialData[cascade * RESOLUTION * RESOLUTION +
                       wrapped.y * RESOLUTION + wrapped.x];
}

fn resolvedDisplacement(coord : vec2<u32>, cascade : u32) -> vec3<f32> {
    let wave = loadWave(coord, cascade);
    let sign = select(-1.0, 1.0, ((coord.x + coord.y) & 1u) == 0u);
    let amplitude = cascadeAmplitude(cascade);
    return vec3<f32>(wave.displacementX.x * amplitude * params.choppiness,
                     -wave.height.x * amplitude,
                     wave.displacementZ.x * amplitude * params.choppiness) * sign;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x >= RESOLUTION || gid.y >= RESOLUTION || gid.z >= CASCADE_COUNT) {
        return;
    }

    let coord = gid.xy;
    let cascade = gid.z;
    let center = resolvedDisplacement(coord, cascade);
    let left = resolvedDisplacement(
        coord + vec2<u32>(RESOLUTION - 1u, 0u), cascade);
    let right = resolvedDisplacement(coord + vec2<u32>(1u, 0u), cascade);
    let down = resolvedDisplacement(
        coord + vec2<u32>(0u, RESOLUTION - 1u), cascade);
    let up = resolvedDisplacement(coord + vec2<u32>(0u, 1u), cascade);

    let derivativeScale = 0.5 * f32(RESOLUTION) / patchLength(cascade);
    let derivativeX = (right - left) * derivativeScale;
    let derivativeZ = (up - down) * derivativeScale;
    let tangentX = vec3<f32>(1.0, 0.0, 0.0) + derivativeX;
    let tangentZ = vec3<f32>(0.0, 0.0, 1.0) + derivativeZ;
    var normal = cross(tangentZ, tangentX);
    if (dot(normal, normal) > 1.0e-12) {
        normal = normalize(normal);
    } else {
        normal = vec3<f32>(0.0, 1.0, 0.0);
    }
    let dDxDx = derivativeX.x;
    let dDxDz = derivativeZ.x;
    let dDzDx = derivativeX.z;
    let dDzDz = derivativeZ.z;
    let jacobian = (1.0 + dDxDx) * (1.0 + dDzDz) - dDxDz * dDzDx;
    let compression = clamp(1.0 - jacobian, 0.0, 2.0);

    textureStore(outputTexture, vec2<i32>(coord), i32(cascade),
                 vec4<f32>(center, compression));
    textureStore(outputTexture, vec2<i32>(coord),
                 i32(cascade + CASCADE_COUNT),
                 vec4<f32>(normal, compression));
}
