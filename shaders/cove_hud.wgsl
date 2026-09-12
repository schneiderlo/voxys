@group(0) @binding(0) var coverage: texture_2d<f32>;
@group(0) @binding(1) var coverageSampler: sampler;
struct Out {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
};
@vertex fn vs(@builtin(vertex_index) index:u32,
    @location(0) bounds:vec4<f32>, @location(1) uv:vec4<f32>,
    @location(2) color:vec4<f32>) -> Out {
    var corners=array<vec2<f32>,6>(vec2(0,0),vec2(1,0),vec2(0,1),
        vec2(0,1),vec2(1,0),vec2(1,1));
    let p=corners[index];
    var out:Out;
    out.position=vec4(bounds.xy+p*bounds.zw,0,1);
    out.uv=uv.xy+p*uv.zw;
    out.color=color;
    return out;
}
@fragment fn fs(input:Out) -> @location(0) vec4<f32> {
    return vec4(input.color.rgb,input.color.a*textureSample(coverage,coverageSampler,input.uv).r);
}
