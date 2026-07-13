// Resolve inverse-FFT complex fields into filterable displacement cascades.

struct WaveData {
    height : vec2<f32>,
    displacementX : vec2<f32>,
    displacementZ : vec2<f32>,
    padding : vec2<f32>,
};

@group(0) @binding(0) var<storage, read> spatialData : array<WaveData>;
@group(0) @binding(1) var outputTexture : texture_storage_2d_array<rgba16float, write>;

const RESOLUTION : u32 = 256u;
const CASCADE_COUNT : u32 = 3u;

fn patchLength(cascade : u32) -> f32 {
    switch cascade {
        case 0u: { return 96.0; }
        case 1u: { return 384.0; }
        default: { return 1536.0; }
    }
}

fn choppiness(cascade : u32) -> f32 {
    switch cascade {
        case 0u: { return 0.72; }
        case 1u: { return 1.05; }
        default: { return 1.22; }
    }
}

fn loadWave(coord : vec2<u32>, cascade : u32) -> WaveData {
    let wrapped = coord & vec2<u32>(RESOLUTION - 1u);
    return spatialData[cascade * RESOLUTION * RESOLUTION +
                       wrapped.y * RESOLUTION + wrapped.x];
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x >= RESOLUTION || gid.y >= RESOLUTION || gid.z >= CASCADE_COUNT) {
        return;
    }

    let coord = gid.xy;
    let cascade = gid.z;
    let center = loadWave(coord, cascade);
    let left = loadWave(coord + vec2<u32>(RESOLUTION - 1u, 0u), cascade);
    let right = loadWave(coord + vec2<u32>(1u, 0u), cascade);
    let down = loadWave(coord + vec2<u32>(0u, RESOLUTION - 1u), cascade);
    let up = loadWave(coord + vec2<u32>(0u, 1u), cascade);

    let chop = choppiness(cascade);
    let derivativeScale = 0.5 * f32(RESOLUTION) / patchLength(cascade);
    let dDxDx = (right.displacementX.x - left.displacementX.x) * derivativeScale * chop;
    let dDxDz = (up.displacementX.x - down.displacementX.x) * derivativeScale * chop;
    let dDzDx = (right.displacementZ.x - left.displacementZ.x) * derivativeScale * chop;
    let dDzDz = (up.displacementZ.x - down.displacementZ.x) * derivativeScale * chop;
    let dHeightDx = (right.height.x - left.height.x) * derivativeScale;
    let dHeightDz = (up.height.x - down.height.x) * derivativeScale;
    let jacobian = (1.0 + dDxDx) * (1.0 + dDzDz) - dDxDz * dDzDx;
    let compression = clamp(1.0 - jacobian, 0.0, 2.0);

    textureStore(outputTexture, vec2<i32>(coord), i32(cascade),
                 vec4<f32>(center.height.x,
                           dHeightDx,
                           dHeightDz,
                           compression));
}
