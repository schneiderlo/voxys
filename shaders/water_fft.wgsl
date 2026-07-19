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
@group(0) @binding(3) var<storage, read> twiddleData : array<vec2<f32>>;

const PI : f32 = 3.141592653589793;
const GRAVITY : f32 = 9.81;
const CASCADE_COUNT : u32 = 3u;

// One 256-sample row or column. Keeping all eight radix-2 stages here avoids
// round-tripping the complete spectrum through device memory for every stage.
var<workgroup> lineData : array<WaveData, 256>;

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
    let initial = inputData[index];
    let omega = initial.padding.x;
    // This inverse FFT uses +i*k*x. A negative temporal phase therefore makes
    // wind-favoured +k modes travel along +k instead of away from the wind.
    let phase = -omega * params.time;
    let positive = vec2<f32>(cos(phase), sin(phase));
    let negative = vec2<f32>(positive.x, -positive.y);
    let h = complexMul(initial.height, positive) +
            complexMul(initial.displacementX, negative);

    var result : WaveData;
    result.height = h;
    if (initial.padding.y > 0.0) {
        let ih = vec2<f32>(-h.y, h.x);
        let invIndexLength = initial.padding.y;
        result.displacementX = ih * (-sx * invIndexLength);
        result.displacementZ = ih * (-sy * invIndexLength);
    } else {
        result.displacementX = vec2<f32>(0.0);
        result.displacementZ = vec2<f32>(0.0);
    }
    result.padding = vec2<f32>(0.0);
    outputData[index] = result;
}

@compute @workgroup_size(256, 1, 1)
fn fftAxis(@builtin(local_invocation_id) lid : vec3<u32>,
           @builtin(workgroup_id) wid : vec3<u32>) {
    let n = params.size;
    let sample = lid.x;
    let cascade = wid.x / n;
    let line = wid.x - cascade * n;
    let base = cascade * n * n;

    var readCoord = vec2<u32>(bitReverse8(sample), line);
    if (params.axis == 1u) {
        readCoord = readCoord.yx;
    }
    lineData[sample] = outputData[base + readCoord.y * n + readCoord.x];
    workgroupBarrier();

    var stage = 0u;
    loop {
        if (stage >= 8u) { break; }
        if (sample < n / 2u) {
            let halfSpan = 1u << stage;
            let span = halfSpan << 1u;
            let group = sample / halfSpan;
            let j = sample - group * halfSpan;
            let i0 = group * span + j;
            let i1 = i0 + halfSpan;
            let twiddle = twiddleData[halfSpan - 1u + j];
            let a = lineData[i0];
            let b = mulWave(lineData[i1], twiddle);
            lineData[i0] = addWave(a, b);
            lineData[i1] = subWave(a, b);
        }
        workgroupBarrier();
        stage += 1u;
    }

    var writeCoord = vec2<u32>(sample, line);
    if (params.axis == 1u) {
        writeCoord = writeCoord.yx;
    }
    outputData[base + writeCoord.y * n + writeCoord.x] = lineData[sample];
}
