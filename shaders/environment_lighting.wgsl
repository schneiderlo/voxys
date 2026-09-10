// Linear radiance convolution and split-sum GGX integration. This producer is
// independent of display exposure/tone mapping. See the environment-lighting
// validation design for conventions, analytic references and approximation limits.
struct BakeParams { size: u32, kind: u32, samples: u32, roughness: f32 }
@group(0) @binding(0) var<uniform> params: BakeParams;
@group(0) @binding(1) var source: texture_2d<f32>;
@group(0) @binding(2) var sourceSampler: sampler;
@group(0) @binding(3) var output: texture_storage_2d_array<rgba16float, write>;
const PI = 3.141592653589793;

fn faceDirection(pixel: vec2<u32>, face: u32) -> vec3<f32> {
    let p = 2.0 * (vec2<f32>(pixel) + 0.5) / f32(params.size) - 1.0;
    var direction = vec3<f32>(-p.x, -p.y, -1.0);
    switch face {
        case 0u: { direction = vec3<f32>(1.0, -p.y, -p.x); }
        case 1u: { direction = vec3<f32>(-1.0, -p.y, p.x); }
        case 2u: { direction = vec3<f32>(p.x, 1.0, p.y); }
        case 3u: { direction = vec3<f32>(p.x, -1.0, -p.y); }
        case 4u: { direction = vec3<f32>(p.x, -p.y, 1.0); }
        default: {}
    }
    return normalize(direction);
}

fn sequence(i: u32) -> vec2<f32> {
    return vec2<f32>((f32(i) + 0.5) / f32(params.samples),
                     f32(reverseBits(i)) * 2.3283064365386963e-10);
}

fn frame(n: vec3<f32>) -> mat3x3<f32> {
    let axis = select(vec3<f32>(0.0, 0.0, 1.0), vec3<f32>(1.0, 0.0, 0.0), abs(n.z) > 0.999);
    let t = normalize(cross(axis, n));
    return mat3x3<f32>(t, cross(n, t), n);
}

fn radiance(direction: vec3<f32>, pdf: f32) -> vec3<f32> {
    let uv = vec2<f32>(atan2(direction.z, direction.x) / (2.0 * PI) + 0.5,
                       acos(clamp(direction.y, -1.0, 1.0)) / PI);
    let dimensions = vec2<f32>(textureDimensions(source, 0));
    // Account for shrinking equirectangular texels near the poles. A finite
    // polar cap keeps the footprint bounded at exactly vertical directions.
    let sinTheta = max(length(direction.xz), sin(0.5 * PI / dimensions.y));
    let texelAngle = 2.0 * PI * PI * sinTheta / (dimensions.x * dimensions.y);
    let sampleAngle = 1.0 / max(f32(params.samples) * pdf, 1.0e-8);
    var lod = 0.0;
    if (pdf > 0.0) {
        lod = clamp(0.5 * log2(sampleAngle / texelAngle), 0.0, f32(textureNumLevels(source) - 1u));
    }
    return textureSampleLevel(source, sourceSampler, uv, lod).rgb;
}

fn halfVector(xi: vec2<f32>, roughness: f32) -> vec3<f32> {
    let alpha = roughness * roughness;
    let cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    let sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    let phi = 2.0 * PI * xi.x;
    return vec3<f32>(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

fn convolve(n: vec3<f32>) -> vec3<f32> {
    if (params.kind == 0u && params.roughness == 0.0) { return radiance(n, 0.0); }
    let basis = frame(n);
    var sum = vec3<f32>(0.0);
    var weight = 0.0;
    for (var i = 0u; i < params.samples; i++) {
        let xi = sequence(i);
        if (params.kind == 1u) {
            // Cosine sampling stores E/pi directly: average incoming radiance.
            let radius = sqrt(xi.y);
            let local = vec3<f32>(radius * cos(2.0 * PI * xi.x),
                                   radius * sin(2.0 * PI * xi.x), sqrt(1.0 - xi.y));
            sum += radiance(basis * local, local.z / PI);
            weight += 1.0;
        } else {
            // Split-sum prefilter assumes N=V. Roughness is perceptual; alpha=r².
            let h = halfVector(xi, params.roughness);
            let l = 2.0 * h.z * h - vec3<f32>(0.0, 0.0, 1.0);
            if (l.z > 0.0) {
                let alpha2 = pow(params.roughness, 4.0);
                let denominator = h.z * h.z * (alpha2 - 1.0) + 1.0;
                let distribution = alpha2 / (PI * denominator * denominator);
                // pdf(L)=D*NoH/(4*VoH)=D/4 for this N=V prefilter.
                sum += radiance(basis * l, distribution * 0.25) * l.z;
                weight += l.z;
            }
        }
    }
    return sum / max(weight, 1.0e-8);
}

fn integrateBrdf(uv: vec2<f32>) -> vec2<f32> {
    let noV = uv.x;
    let roughness = uv.y;
    let v = vec3<f32>(sqrt(1.0 - noV * noV), 0.0, noV);
    // IBL visibility has no direct-light hotness remap.
    let k = roughness * roughness * 0.5;
    let gV = noV / (noV * (1.0 - k) + k);
    var result = vec2<f32>(0.0);
    for (var i = 0u; i < params.samples; i++) {
        let h = halfVector(sequence(i), roughness);
        let voH = max(dot(v, h), 0.0);
        let l = 2.0 * voH * h - v;
        if (l.z > 0.0 && voH > 0.0) {
            let gL = l.z / (l.z * (1.0 - k) + k);
            let visibility = gV * gL * voH / max(h.z * noV, 1.0e-8);
            let fc = pow(1.0 - voH, 5.0);
            result += vec2<f32>(1.0 - fc, fc) * visibility;
        }
    }
    return result / f32(params.samples);
}

@compute @workgroup_size(8, 8, 1)
fn bake(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= params.size || id.y >= params.size) { return; }
    var value: vec4<f32>;
    if (params.kind == 2u) {
        let ab = integrateBrdf((vec2<f32>(id.xy) + 0.5) / f32(params.size));
        value = vec4<f32>(ab, 0.0, 1.0);
    } else {
        value = vec4<f32>(convolve(faceDirection(id.xy, id.z)), 1.0);
    }
    textureStore(output, vec2<i32>(id.xy), i32(id.z), value);
}
