// One instance per CPU-projected actual-mesh triangle. No world or font state.
struct Out {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
};
@vertex fn vs(@builtin(vertex_index) index:u32,
    @location(0) a:vec2<f32>, @location(1) b:vec2<f32>,
    @location(2) c:vec2<f32>, @location(3) color:vec4<f32>) -> Out {
    var points=array<vec2<f32>,3>(a,b,c);
    var out:Out;
    out.position=vec4(points[index],0,1);
    out.color=color;
    return out;
}
@fragment fn fs(input:Out) -> @location(0) vec4<f32> {
    return input.color;
}
