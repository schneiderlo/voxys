struct Sample { feet: array<vec4<f32>,2>, position: vec4<f32>, normal: vec4<f32> };
@group(0) @binding(0) var<storage,read> samples:array<Sample>;
@group(0) @binding(1) var<storage,read_write> outputs:array<vec2<u32>>;
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
        let radial = 1.0 - smoothstep(0.0, radius, length(horizontal));
        occlusion = max(occlusion, 0.68 * vertical * radial * max(normal.y, 0.0));
    }
    return 1.0 - occlusion;
}

@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid:vec3<u32>) {
 if(gid.x>=arrayLength(&samples)) {return;} let s=samples[gid.x];
 outputs[gid.x]=vec2<u32>(bitcast<u32>(baselineFoot(s.position.xyz,s.normal.xyz,s.feet)),bitcast<u32>(candidateFoot(s.position.xyz,s.normal.xyz,s.feet)));
}
