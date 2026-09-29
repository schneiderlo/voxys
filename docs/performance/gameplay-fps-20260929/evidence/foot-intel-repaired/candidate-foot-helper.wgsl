fn footContactVisibility(position: vec3<f32>, normal: vec3<f32>) -> f32 {
    var occlusion = 0.0;
    for (var i = 0u; i < 2u; i += 1u) {
        let foot = sunShadow.footContacts[i];
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
