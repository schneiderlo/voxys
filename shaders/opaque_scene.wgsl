// Immutable terrain cache -> separate per-frame opaque scene. No filtering,
// exposure or output transform: water needs linear HDR and radial metres.
@group(0) @binding(0) var terrainColor : texture_2d<f32>;
@group(0) @binding(1) var terrainDepth : texture_2d<f32>;

@vertex fn vs(@builtin(vertex_index) index : u32) -> @builtin(position) vec4<f32> {
    let corner = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
struct Output {
    @location(0) color : vec4<f32>,
    @location(1) depth : f32,
};
@fragment fn fs(@builtin(position) position : vec4<f32>) -> Output {
    let pixel = vec2<i32>(position.xy);
    return Output(textureLoad(terrainColor, pixel, 0), textureLoad(terrainDepth, pixel, 0).x);
}
