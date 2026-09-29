
struct Sample { feet: array<vec4<f32>, 2>, position: vec4<f32>, normal: vec4<f32> };
@group(0) @binding(0) var<uniform> samples: array<Sample, 512>;
var<private> lengthCalls: u32;
fn baselineFoot(position: vec3<f32>, normal: vec3<f32>, feet: array<vec4<f32>, 2>) -> f32 {
    var occlusion = 0.0;
    for (var i = 0u; i < 2u; i += 1u) {
        let foot = feet[i];
        if (foot.w <= 0.0) { continue; }
        let height = foot.y - position.y;
        let reach = foot.w * 2.0;
        let vertical = (1.0 - smoothstep(0.0, reach, max(height, 0.0)))
            * smoothstep(-0.06, 0.0, height);
        let radius = foot.w + max(height, 0.0) * 0.35;
        lengthCalls += 1u;
        let radial = 1.0 - smoothstep(0.0, radius, length(position.xz - foot.xz));
        occlusion = max(occlusion, 0.68 * vertical * radial * max(normal.y, 0.0));
    }
    return 1.0 - occlusion;
}
fn candidateFoot(position: vec3<f32>, normal: vec3<f32>, feet: array<vec4<f32>, 2>) -> f32 {
    var occlusion = 0.0;
    for (var i = 0u; i < 2u; i += 1u) {
        let foot = feet[i];
        if (foot.w <= 0.0) { continue; }
        let height = foot.y - position.y;
        let reach = foot.w * 2.0;
        // Keep the original smoothstep math near support boundaries. GPU
        // length/division rounding can retain a contribution one ulp outside.
        if (height < -0.0601 || height > reach * 1.00001 + 0.0001) { continue; }
        let radius = foot.w + max(height, 0.0) * 0.35;
        let horizontal = position.xz - foot.xz;
        let radialLimit = radius * 1.00001 + 0.0001;
        if (any(abs(horizontal) > vec2<f32>(radialLimit))) { continue; }
        let vertical = (1.0 - smoothstep(0.0, reach, max(height, 0.0)))
            * smoothstep(-0.06, 0.0, height);
        lengthCalls += 1u;
        let radial = 1.0 - smoothstep(0.0, radius, length(horizontal));
        occlusion = max(occlusion, 0.68 * vertical * radial * max(normal.y, 0.0));
    }
    return 1.0 - occlusion;
}

struct ProbeOutput { @location(0) before: vec4<f32>, @location(1) after: vec4<f32>, @location(2) work: vec4<f32> };
fn encodedBits(value: f32) -> vec4<f32> {
 let bits = bitcast<u32>(value);
 return vec4<f32>(f32(bits & 255u), f32((bits >> 8u) & 255u),
    f32((bits >> 16u) & 255u), f32(bits >> 24u)) / 255.0;
}
@vertex fn vertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
 let x = f32((index << 1u) & 2u); let y = f32(index & 2u);
 return vec4<f32>(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.5, 1.0);
}
@fragment fn test(@builtin(position) pixel: vec4<f32>) -> ProbeOutput {
 let index = u32(pixel.y) * 32u + u32(pixel.x);
 let sample = samples[index];
 lengthCalls = 0u;
 let before = baselineFoot(sample.position.xyz, sample.normal.xyz, sample.feet);
 let beforeWork = lengthCalls;
 lengthCalls = 0u;
 let after = candidateFoot(sample.position.xyz, sample.normal.xyz, sample.feet);
 return ProbeOutput(encodedBits(before), encodedBits(after),
    vec4<f32>(f32(beforeWork), f32(lengthCalls), 0.0, 255.0) / 255.0);
}

