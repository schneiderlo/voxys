// Tessendorf spectrum evolution and separable inverse FFT.

struct SimParams {
    time : f32,
    stage : u32,
    axis : u32,
    size : u32,
};

struct WaveData {
    height : vec2<f32>,
    displacementX : vec2<f32>,
    displacementZ : vec2<f32>,
    padding : vec2<f32>,
};

@group(0) @binding(0) var<uniform> params : SimParams;
@group(0) @binding(1) var<storage, read> inputData : array<WaveData>;
@group(0) @binding(2) var<storage, read_write> outputData : array<WaveData>;

const PI : f32 = 3.141592653589793;
const GRAVITY : f32 = 9.81;
const CASCADE_COUNT : u32 = 3u;

fn patchLength(cascade : u32) -> f32 {
    switch cascade {
        case 0u: { return 96.0; }
        case 1u: { return 384.0; }
        default: { return 1536.0; }
    }
}

fn complexMul(a : vec2<f32>, b : vec2<f32>) -> vec2<f32> {
    return vec2<f32>(a.x * b.x - a.y * b.y,
                     a.x * b.y + a.y * b.x);
}

fn bitReverse8(value : u32) -> u32 {
    var v = value;
    v = ((v & 0x55u) << 1u) | ((v >> 1u) & 0x55u);
    v = ((v & 0x33u) << 2u) | ((v >> 2u) & 0x33u);
    v = ((v & 0x0fu) << 4u) | ((v >> 4u) & 0x0fu);
    return v;
}

fn addWave(a : WaveData, b : WaveData) -> WaveData {
    var result : WaveData;
    result.height = a.height + b.height;
    result.displacementX = a.displacementX + b.displacementX;
    result.displacementZ = a.displacementZ + b.displacementZ;
    result.padding = vec2<f32>(0.0);
    return result;
}

fn subWave(a : WaveData, b : WaveData) -> WaveData {
    var result : WaveData;
    result.height = a.height - b.height;
    result.displacementX = a.displacementX - b.displacementX;
    result.displacementZ = a.displacementZ - b.displacementZ;
    result.padding = vec2<f32>(0.0);
    return result;
}

fn mulWave(value : WaveData, twiddle : vec2<f32>) -> WaveData {
    var result : WaveData;
    result.height = complexMul(value.height, twiddle);
    result.displacementX = complexMul(value.displacementX, twiddle);
    result.displacementZ = complexMul(value.displacementZ, twiddle);
    result.padding = vec2<f32>(0.0);
    return result;
}

@compute @workgroup_size(256, 1, 1)
fn evolve(@builtin(global_invocation_id) gid : vec3<u32>) {
    let n = params.size;
    let layerStride = n * n;
    let total = layerStride * CASCADE_COUNT;
    let index = gid.x;
    if (index >= total) { return; }

    let cascade = index / layerStride;
    let local = index - cascade * layerStride;
    let x = local % n;
    let y = local / n;
    let sx = select(f32(x), f32(i32(x) - i32(n)), x > n / 2u);
    let sy = select(f32(y), f32(i32(y) - i32(n)), y > n / 2u);
    let k = vec2<f32>(sx, sy) * (2.0 * PI / patchLength(cascade));
    let kLength = length(k);

    let initial = inputData[index];
    let omega = sqrt(GRAVITY * kLength);
    let phase = omega * params.time;
    let positive = vec2<f32>(cos(phase), sin(phase));
    let negative = vec2<f32>(positive.x, -positive.y);
    let h = complexMul(initial.height, positive) +
            complexMul(initial.displacementX, negative);

    var result : WaveData;
    result.height = h;
    if (kLength > 1e-5) {
        let ih = vec2<f32>(-h.y, h.x);
        result.displacementX = ih * (-k.x / kLength);
        result.displacementZ = ih * (-k.y / kLength);
    } else {
        result.displacementX = vec2<f32>(0.0);
        result.displacementZ = vec2<f32>(0.0);
    }
    result.padding = vec2<f32>(0.0);
    outputData[index] = result;
}

@compute @workgroup_size(256, 1, 1)
fn fft(@builtin(global_invocation_id) gid : vec3<u32>) {
    let n = params.size;
    let butterfliesPerLayer = n * n / 2u;
    let totalButterflies = butterfliesPerLayer * CASCADE_COUNT;
    let butterfly = gid.x;
    if (butterfly >= totalButterflies) { return; }

    let cascade = butterfly / butterfliesPerLayer;
    let localButterfly = butterfly - cascade * butterfliesPerLayer;
    let line = localButterfly / (n / 2u);
    let along = localButterfly % (n / 2u);
    let halfSpan = 1u << params.stage;
    let span = halfSpan << 1u;
    let group = along / halfSpan;
    let j = along - group * halfSpan;
    let i0 = group * span + j;
    let i1 = i0 + halfSpan;

    var coord0 = vec2<u32>(i0, line);
    var coord1 = vec2<u32>(i1, line);
    if (params.axis == 1u) {
        coord0 = coord0.yx;
        coord1 = coord1.yx;
    }
    if (params.stage == 0u) {
        if (params.axis == 0u) {
            coord0.x = bitReverse8(coord0.x);
            coord1.x = bitReverse8(coord1.x);
        } else {
            coord0.y = bitReverse8(coord0.y);
            coord1.y = bitReverse8(coord1.y);
        }
    }

    let base = cascade * n * n;
    let read0 = base + coord0.y * n + coord0.x;
    let read1 = base + coord1.y * n + coord1.x;
    let angle = 2.0 * PI * f32(j) / f32(span);
    let twiddle = vec2<f32>(cos(angle), sin(angle));
    let a = inputData[read0];
    let b = mulWave(inputData[read1], twiddle);

    var outCoord0 = vec2<u32>(i0, line);
    var outCoord1 = vec2<u32>(i1, line);
    if (params.axis == 1u) {
        outCoord0 = outCoord0.yx;
        outCoord1 = outCoord1.yx;
    }
    outputData[base + outCoord0.y * n + outCoord0.x] = addWave(a, b);
    outputData[base + outCoord1.y * n + outCoord1.x] = subWave(a, b);
}
