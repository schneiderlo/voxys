// ═══════════════════════════════════════════════════════════════════════════════
// sky_lut.wgsl - Static procedural environment bake
// ═══════════════════════════════════════════════════════════════════════════════
// The sky depends only on the view direction and nothing in it animates
// (fixed sun, static clouds). This compute pass renders the expensive parts
// once — atmospheric scattering, base gradient, FBM clouds — into a small
// equirectangular HDR texture. Per frame, the blit pass replaces all of
// that with a single filtered sample. The environment is deliberately kept in
// scene-linear HDR. A broad directional emitter is part of the environment,
// so it remains visible in rough water reflections after mip filtering.
//
// A full-sphere map is required for steep water normals: their reflected ray
// may enter the lower hemisphere even when the camera is above the surface.
// ═══════════════════════════════════════════════════════════════════════════════

struct CameraUniforms {
    viewProj : mat4x4<f32>,
    invViewProj : mat4x4<f32>,
    invView : mat4x4<f32>,
    terrainSize : vec2<f32>,
    invTerrainSize : vec2<f32>,
    metrics : vec4<f32>,          // (heightScale, cellScale, step, fogDensity)
    cameraPos : vec4<f32>,
    invProjParams : vec4<f32>,
    lightDirVS : vec4<f32>,
    frustumPlanes : array<vec4<f32>, 6>,
    lightDirWS : vec4<f32>,
    waterParams : vec4<f32>,
    waterColorA : vec4<f32>,
    waterColorB : vec4<f32>,
    waterMotion : vec4<f32>,
    lightingColor : vec4<f32>,
    ambientExposure : vec4<f32>,
    fogColor : vec4<f32>,
    waterOptics : vec4<f32>,
    waterFoam : vec4<f32>,
    waterSpectrum : vec4<f32>,
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var outSky : texture_storage_2d<rgba16float, write>;

// ─────────────────────────────────────────────────────────────────────────────
// Noise (same construction as the water/cloud noise in ray_blit.wgsl)
// ─────────────────────────────────────────────────────────────────────────────

fn hash2D(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn grad2D(hash: f32) -> vec2<f32> {
    switch (u32(hash * 8.0) & 7u) {
        case 0u: { return vec2<f32>(1.0, 0.0); }
        case 1u: { return vec2<f32>(0.70710678, 0.70710678); }
        case 2u: { return vec2<f32>(0.0, 1.0); }
        case 3u: { return vec2<f32>(-0.70710678, 0.70710678); }
        case 4u: { return vec2<f32>(-1.0, 0.0); }
        case 5u: { return vec2<f32>(-0.70710678, -0.70710678); }
        case 6u: { return vec2<f32>(0.0, -1.0); }
        default: { return vec2<f32>(0.70710678, -0.70710678); }
    }
}

fn simplexNoise2D(p: vec2<f32>) -> f32 {
    let F2 = 0.366025403784;
    let G2 = 0.211324865405;

    let s = (p.x + p.y) * F2;
    let i = floor(p.x + s);
    let j = floor(p.y + s);

    let t = (i + j) * G2;
    let X0 = i - t;
    let Y0 = j - t;
    let x0 = p.x - X0;
    let y0 = p.y - Y0;

    var i1: f32;
    var j1: f32;
    if (x0 > y0) {
        i1 = 1.0; j1 = 0.0;
    } else {
        i1 = 0.0; j1 = 1.0;
    }

    let x1 = x0 - i1 + G2;
    let y1 = y0 - j1 + G2;
    let x2 = x0 - 1.0 + 2.0 * G2;
    let y2 = y0 - 1.0 + 2.0 * G2;

    var n0 = 0.0;
    var n1 = 0.0;
    var n2 = 0.0;

    var t0 = 0.5 - x0*x0 - y0*y0;
    if (t0 >= 0.0) {
        t0 = t0 * t0;
        let g0 = grad2D(hash2D(vec2<f32>(i, j)));
        n0 = t0 * t0 * dot(g0, vec2<f32>(x0, y0));
    }

    var t1 = 0.5 - x1*x1 - y1*y1;
    if (t1 >= 0.0) {
        t1 = t1 * t1;
        let g1 = grad2D(hash2D(vec2<f32>(i + i1, j + j1)));
        n1 = t1 * t1 * dot(g1, vec2<f32>(x1, y1));
    }

    var t2 = 0.5 - x2*x2 - y2*y2;
    if (t2 >= 0.0) {
        t2 = t2 * t2;
        let g2 = grad2D(hash2D(vec2<f32>(i + 1.0, j + 1.0)));
        n2 = t2 * t2 * dot(g2, vec2<f32>(x2, y2));
    }

    return 70.0 * (n0 + n1 + n2);
}

// Blend three planar noise evaluations by the sphere normal. Unlike a cloud
// plane or longitude/latitude noise, this remains continuous at both the
// equirectangular seam and the poles. Those discontinuities are especially
// objectionable in water because rough reflection magnifies them into large
// rectangular patches.
fn sphericalNoise(direction : vec3<f32>, frequency : f32,
                  offset : vec3<f32>) -> f32 {
    let p = normalize(direction);
    var weights = pow(abs(p), vec3<f32>(4.0));
    weights /= max(weights.x + weights.y + weights.z, 1.0e-5);
    let alongX = simplexNoise2D(p.yz * frequency + offset.yz);
    let alongY = simplexNoise2D(p.zx * frequency + offset.zx);
    let alongZ = simplexNoise2D(p.xy * frequency + offset.xy);
    return dot(vec3<f32>(alongX, alongY, alongZ), weights);
}

fn equirectangularDirection(uv : vec2<f32>) -> vec3<f32> {
    let longitude = (uv.x - 0.5) * 6.283185307179586;
    let latitude = (0.5 - uv.y) * 3.141592653589793;
    let latitudeCosine = cos(latitude);
    return vec3<f32>(latitudeCosine * cos(longitude), sin(latitude),
                     latitudeCosine * sin(longitude));
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky Radiance (linear HDR, no sun disc / bloom / tonemap)
// ─────────────────────────────────────────────────────────────────────────────

fn skyRadiance(worldDir : vec3<f32>) -> vec3<f32> {
    let sunDir = normalize(camera.lightDirWS.xyz);
    let sunDot = clamp(dot(worldDir, sunDir), -1.0, 1.0);
    if (worldDir.y <= 0.0) {
        let horizon = smoothstep(-0.55, 0.02, worldDir.y);
        let lowerSky = mix(vec3<f32>(0.025, 0.040, 0.065),
                           vec3<f32>(0.27, 0.39, 0.52), horizon);
        let reflectedSun = normalize(vec3<f32>(sunDir.x, -sunDir.y, sunDir.z));
        let lowerGlow = pow(max(dot(worldDir, reflectedSun), 0.0), 18.0);
        return lowerSky + vec3<f32>(0.55, 0.67, 0.78) * lowerGlow;
    }

    let elevation = clamp(worldDir.y, 0.0, 1.0);
    let horizonColor = vec3<f32>(0.40, 0.54, 0.68);
    let zenithColor = vec3<f32>(0.085, 0.205, 0.390);
    let heightGradient = pow(elevation, 0.42);
    var skyColor = mix(horizonColor, zenithColor, heightGradient);

    // The generated environment needs the broad directional range an HDR
    // panorama normally supplies: a cool, dark anti-solar side and a bright
    // sunward side. This contrast is what makes the same rough wave field read
    // as water instead of uniform blue plastic.
    let horizontalLength = max(length(worldDir.xz), 1.0e-4);
    let sunHorizontalLength = max(length(sunDir.xz), 1.0e-4);
    let sunAzimuth = dot(worldDir.xz / horizontalLength,
                         sunDir.xz / sunHorizontalLength);
    let directionalExposure = mix(
        0.68, 1.16, smoothstep(-0.72, 0.82, sunAzimuth));
    skyColor *= directionalExposure;

    let horizonHaze = pow(1.0 - elevation, 7.0);
    let forwardHaze = pow(max(sunDot, 0.0), 6.0);
    skyColor += vec3<f32>(0.24, 0.27, 0.29) * horizonHaze *
                (0.45 + 0.55 * forwardHaze);

    // Seamless, domain-warped spherical FBM supplies the large cloud masses
    // that make the rough ocean readable. The bake is one-shot, so this extra
    // structure does not add recurring frame cost.
    let warp = vec3<f32>(
        sphericalNoise(worldDir, 1.7, vec3<f32>(1.7, -3.2, 4.1)),
        sphericalNoise(worldDir, 1.7, vec3<f32>(-4.1, 2.3, 0.8)),
        sphericalNoise(worldDir, 1.7, vec3<f32>(2.6, 5.7, -1.9)));
    let cloudDirection = normalize(worldDir + warp * 0.18);
    var cloudNoise = sphericalNoise(
        cloudDirection, 2.1, vec3<f32>(0.3, 4.7, 1.2)) * 0.45;
    cloudNoise += sphericalNoise(
        cloudDirection, 4.3, vec3<f32>(4.7, 1.2, -2.1)) * 0.27;
    cloudNoise += sphericalNoise(
        cloudDirection, 8.9, vec3<f32>(-2.1, 5.3, 8.2)) * 0.16;
    cloudNoise += sphericalNoise(
        cloudDirection, 18.3, vec3<f32>(8.2, -0.8, 3.4)) * 0.08;
    cloudNoise += sphericalNoise(
        cloudDirection, 37.1, vec3<f32>(-3.4, 9.1, 6.8)) * 0.04;
    let erosion = abs(sphericalNoise(
        cloudDirection, 27.0, vec3<f32>(-1.9, 7.4, 3.1)));
    let density = cloudNoise - erosion * 0.10;
    let cloudBody = smoothstep(-0.12, 0.25, density);
    let altitudeBand = smoothstep(0.015, 0.11, elevation) *
                       (1.0 - 0.30 * smoothstep(0.78, 1.0, elevation));
    let cloudAlpha = cloudBody * altitudeBand *
                     mix(0.82, 0.58, elevation);
    let billowLight = sphericalNoise(
        cloudDirection, 5.4, vec3<f32>(5.2, -1.4, 2.9) - sunDir * 0.28);
    let cloudLight = clamp(
        0.16 + 0.60 * smoothstep(-0.32, 0.86, sunDot) +
        0.24 * smoothstep(-0.45, 0.45, billowLight), 0.0, 1.0);
    let cloudShadow = vec3<f32>(0.18, 0.235, 0.31);
    let cloudHighlight = vec3<f32>(0.98, 1.02, 1.08);
    let cloudColor = mix(cloudShadow, cloudHighlight, cloudLight);
    skyColor = mix(skyColor, cloudColor, cloudAlpha);
    let silverEdge = (smoothstep(-0.15, -0.01, density) -
                      smoothstep(-0.01, 0.10, density)) * altitudeBand;
    skyColor += vec3<f32>(1.45, 1.38, 1.28) * silverEdge *
                pow(max(sunDot, 0.0), 3.0);

    // Thin high-altitude streaks stop clear gaps from becoming a featureless
    // gradient while retaining large blue openings between the cloud masses.
    let highNoise = sphericalNoise(
        worldDir, 11.0, vec3<f32>(2.6, -1.1, 4.2));
    let highCloud = smoothstep(0.16, 0.54, highNoise) *
                    smoothstep(0.24, 0.55, elevation) * 0.24;
    skyColor = mix(skyColor, vec3<f32>(0.78, 0.88, 1.02), highCloud);

    // The environment's emitter is deliberately broad: its lowest-frequency
    // halo survives roughness mip selection, while the compact HDR core draws
    // sharp moving highlights on nearby crests.
    let positiveSun = max(sunDot, 0.0);
    let halo = pow(positiveSun, 7.0) * 1.55;
    let corona = pow(positiveSun, 42.0) * 6.0;
    let core = pow(positiveSun, 280.0) * 32.0;
    skyColor += vec3<f32>(0.78, 0.90, 1.03) * halo;
    skyColor += vec3<f32>(1.00, 1.00, 0.98) * (corona + core);
    return skyColor;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(outSky);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }

    // Texel center -> full-sphere direction.
    let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5, 0.5)) /
             vec2<f32>(f32(dims.x), f32(dims.y));
    let dir = equirectangularDirection(uv);

    let radiance = skyRadiance(dir);
    textureStore(outSky, vec2<i32>(gid.xy), vec4<f32>(radiance, 1.0));
}
