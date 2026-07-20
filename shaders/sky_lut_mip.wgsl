// Box-filter one level of the baked HDR sky. Each dispatch receives views of
// one source mip and one destination mip, so both texture accesses use level 0
// relative to their views.

@group(0) @binding(0) var sourceMip : texture_2d<f32>;
@group(0) @binding(1) var destinationMip :
    texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let destinationSize = textureDimensions(destinationMip);
    if (gid.x >= destinationSize.x || gid.y >= destinationSize.y) {
        return;
    }

    let sourceSize = textureDimensions(sourceMip);
    let base = gid.xy * 2u;
    let maximum = sourceSize - vec2<u32>(1u);
    let p00 = vec2<i32>(min(base, maximum));
    let p10 = vec2<i32>(min(base + vec2<u32>(1u, 0u), maximum));
    let p01 = vec2<i32>(min(base + vec2<u32>(0u, 1u), maximum));
    let p11 = vec2<i32>(min(base + vec2<u32>(1u), maximum));
    let filtered = (textureLoad(sourceMip, p00, 0) +
                    textureLoad(sourceMip, p10, 0) +
                    textureLoad(sourceMip, p01, 0) +
                    textureLoad(sourceMip, p11, 0)) * 0.25;
    textureStore(destinationMip, vec2<i32>(gid.xy), filtered);
}
