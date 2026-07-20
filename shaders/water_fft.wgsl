// Peaked directional spectrum evolution and separable inverse FFT.

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

const CASCADE_COUNT : u32 = 2u;
const DIRECTIONAL_SINE_SCALE : f32 = 0.68;

// One 256-sample row or column. Keeping all eight radix-2 stages here avoids
// round-tripping the complete spectrum through device memory for every stage.
var<workgroup> lineData : array<WaveData, 256>;

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
    let initial = inputData[index];
    let omega = initial.padding.x;
    let phaseCosine = cos(omega * params.time);
    let phaseSine = sin(omega * params.time) * DIRECTIONAL_SINE_SCALE;
    let b = initial.height.x * phaseCosine +
            initial.height.y * phaseSine;
    let c = initial.displacementX.x * phaseCosine +
            initial.displacementX.y * phaseSine;
    let normalizedK = initial.displacementZ;

    var result : WaveData;
    result.height = vec2<f32>(b, c);
    result.displacementX = vec2<f32>(c * normalizedK.x,
                                     -b * normalizedK.x);
    result.displacementZ = vec2<f32>(c * normalizedK.y,
                                     -b * normalizedK.y);
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
