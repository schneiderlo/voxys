// ═══════════════════════════════════════════════════════════════════════════════
// sky_lut.wgsl - Static Sky Lookup Table Bake
// ═══════════════════════════════════════════════════════════════════════════════
// The sky depends only on the view direction and nothing in it animates
// (fixed sun, static clouds). This compute pass renders the expensive parts
// once — atmospheric scattering, base gradient, FBM clouds — into a small
// paraboloid-mapped HDR texture. Per frame, the blit pass replaces all of
// that with a single filtered sample and only adds the analytic sun disc
// and bloom, which need more angular precision than the LUT stores.
//
// Mapping (paraboloid, no wrap seam):
//   uv     = dir.xz / (1 + max(dir.y, 0)) * 0.5 + 0.5
//   dir(p) = ((2p) / (1+r²), (1-r²) / (1+r²)) with p = uv*2-1, r² = dot(p,p)
// Directions below the horizon land at r² > 1 and produce the horizon color,
// matching the previous analytic behaviour.
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

// ─────────────────────────────────────────────────────────────────────────────
// Sky Radiance (linear HDR, no sun disc / bloom / tonemap)
// ─────────────────────────────────────────────────────────────────────────────

fn skyRadiance(worldDir : vec3<f32>) -> vec3<f32> {
    let sunDir = normalize(camera.lightDirWS.xyz);
    let sunDot = dot(worldDir, sunDir);

    // Rayleigh scattering - stronger for shorter wavelengths (blue)
    let rayleighPhase = 0.75 * (1.0 + sunDot * sunDot);
    let rayleighCoeff = vec3<f32>(0.0058, 0.0135, 0.0331);

    // Mie scattering - forward scattering (haze around sun)
    let g = 0.76;
    let g2 = g * g;
    let miePhase = (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * sunDot, 1.5) * 0.25;
    let mieCoeff = vec3<f32>(0.004, 0.004, 0.004);

    // Optical depth increases near horizon
    let zenithAngle = max(worldDir.y, 0.001);
    let opticalDepth = 1.0 / (zenithAngle + 0.15 * pow(93.885 - degrees(acos(zenithAngle)), -1.253));

    let sunIntensity = 22.0;
    let skyScatter = (rayleighCoeff * rayleighPhase + mieCoeff * miePhase) * opticalDepth * sunIntensity;

    // Base sky gradient (zenith to horizon)
    let horizonColor = vec3<f32>(1.08, 0.70, 0.56);
    let zenithColor = vec3<f32>(0.54, 0.68, 0.88);
    let heightGradient = pow(max(worldDir.y, 0.0), 0.4);
    var skyColor = mix(horizonColor, zenithColor, heightGradient) + skyScatter;

    // Procedural clouds (FBM noise) on a fixed-height plane
    if (worldDir.y > 0.0) {
        let cloudHeight = 800.0;
        let t = cloudHeight / max(worldDir.y, 0.001);
        let cloudPos = worldDir.xz * t * 0.0008;
        let cloudOffset = vec2<f32>(camera.metrics.w * 1000.0, 0.0);

        var cloudNoise = 0.0;
        var amplitude = 0.5;
        var frequency = 1.0;
        var noisePos = cloudPos + cloudOffset;
        for (var i = 0; i < 3; i++) {
            cloudNoise += amplitude * simplexNoise2D(noisePos * frequency);
            amplitude *= 0.5;
            frequency *= 2.0;
            noisePos = noisePos * 1.02 + vec2<f32>(0.1, 0.05);
        }

        let cloudDensity = smoothstep(0.1, 0.6, cloudNoise * 0.5 + 0.5);
        let cloudFade = smoothstep(0.0, 0.3, worldDir.y);
        let distanceFade = exp(-t * 0.0003);
        let cloudBrightness = mix(0.7, 1.0, max(0.0, sunDot * 0.5 + 0.5));
        let cloudColor = vec3<f32>(1.0, 0.98, 0.95) * cloudBrightness;
        let cloudAlpha = cloudDensity * cloudFade * distanceFade * 0.8;
        skyColor = mix(skyColor, cloudColor, cloudAlpha);
    }

    return skyColor;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(outSky);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }

    // Texel center -> paraboloid direction
    let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5, 0.5)) /
             vec2<f32>(f32(dims.x), f32(dims.y));
    let p = uv * 2.0 - vec2<f32>(1.0, 1.0);
    let r2 = dot(p, p);
    let dir = normalize(vec3<f32>(2.0 * p.x, 1.0 - r2, 2.0 * p.y) / (1.0 + r2));

    let radiance = skyRadiance(dir);
    textureStore(outSky, vec2<i32>(gid.xy), vec4<f32>(radiance, 1.0));
}
